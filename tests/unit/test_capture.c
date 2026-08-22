/*
 * _DEFAULT_SOURCE让glibc公开libpcap头文件所需的u_int、u_short和
 * u_char等BSD兼容类型。
 *
 * _POSIX_C_SOURCE 200809L让系统头文件公开mkstemp等POSIX.1-2008
 * 接口。
 *
 * 特性测试宏必须放在所有系统头文件之前。
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "analyzer/capture.h"

#include <errno.h>
#include <pcap/pcap.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define TEST_CHECK(condition)                                      \
    do {                                                           \
        if (!(condition)) {                                        \
            fprintf(stderr,                                        \
                    "[FAIL] %s:%d: %s\n",                          \
                    __FILE__,                                      \
                    __LINE__,                                      \
                    #condition);                                   \
            return EXIT_FAILURE;                                   \
        }                                                          \
    } while (false)

/**
 * @brief 创建一个有效但不包含数据包的Ethernet PCAP测试文件。
 *
 * 测试直接调用libpcap只用于准备输入文件。真正被测试的打开、查询和
 * 关闭行为仍然通过项目的capture接口完成。
 *
 * path_template必须以六个X结尾，供mkstemp替换为唯一文件名。
 */
static int create_empty_ethernet_pcap(char *path_template)
{
    int file_descriptor;
    int close_error;
    pcap_t *dead_handle;
    pcap_dumper_t *dumper;

    /*
     * mkstemp以原子方式创建唯一临时文件，避免多个测试进程使用
     * 相同文件名。
     */
    file_descriptor = mkstemp(path_template);

    if (file_descriptor == -1) {
        return errno;
    }

    /*
     * 后面由pcap_dump_open重新打开该路径，因此先关闭mkstemp返回的
     * 文件描述符。
     */
    if (close(file_descriptor) != 0) {
        close_error = errno;
        (void)remove(path_template);
        return close_error;
    }

    /*
     * pcap_open_dead创建一个不抓包的libpcap句柄，只用来描述
     * 链路类型和最大捕获长度。
     */
    dead_handle = pcap_open_dead(DLT_EN10MB, 65535);

    if (dead_handle == NULL) {
        (void)remove(path_template);
        return ENOMEM;
    }

    /*
     * pcap_dump_open会创建PCAP文件头。即使不写入任何数据包，
     * 最终文件仍然是一个合法的空PCAP。
     */
    dumper = pcap_dump_open(dead_handle, path_template);

    if (dumper == NULL) {
        fprintf(stderr,
                "Failed to create test PCAP: %s\n",
                pcap_geterr(dead_handle));

        pcap_close(dead_handle);
        (void)remove(path_template);

        return EIO;
    }

    pcap_dump_close(dumper);
    pcap_close(dead_handle);

    return 0;
}

/**
 * @brief 验证采集模块拒绝无效参数和不存在的文件。
 */
static int test_capture_open_error_handling(void)
{
    char error_buffer[CAPTURE_ERROR_BUFFER_SIZE] = {0};
    capture_t *capture = NULL;
    capture_link_type_t link_type = CAPTURE_LINK_TYPE_UNKNOWN;

    TEST_CHECK(
        capture_open_offline(NULL,
                             &capture,
                             error_buffer,
                             sizeof(error_buffer)) == EINVAL
    );

    TEST_CHECK(capture == NULL);
    TEST_CHECK(error_buffer[0] != '\0');

    TEST_CHECK(
        capture_open_offline("",
                             &capture,
                             error_buffer,
                             sizeof(error_buffer)) == EINVAL
    );

    TEST_CHECK(capture == NULL);

    /*
     * /proc/self下不存在这个普通抓包文件，并且测试不会创建它。
     */
    TEST_CHECK(
        capture_open_offline(
            "/proc/self/netflow-analyzer-missing.pcap",
            &capture,
            error_buffer,
            sizeof(error_buffer)
        ) == EIO
    );

    TEST_CHECK(capture == NULL);
    TEST_CHECK(error_buffer[0] != '\0');

    /*
     * 错误缓冲区和长度没有成对提供。
     */
    TEST_CHECK(
        capture_open_offline(
            "/proc/self/netflow-analyzer-missing.pcap",
            &capture,
            NULL,
            CAPTURE_ERROR_BUFFER_SIZE
        ) == EINVAL
    );

    TEST_CHECK(
        capture_get_link_type(NULL, &link_type) == EINVAL
    );

    TEST_CHECK(capture_get_error(NULL) != NULL);

    /*
     * 关闭NULL是合法空操作，不能导致崩溃。
     */
    capture_close(NULL);
    capture_close(&capture);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证能够打开合法的空Ethernet PCAP并查询链路类型。
 */
static int test_open_empty_ethernet_capture(void)
{
    char pcap_path[] = "/tmp/netflow-analyzer-capture-XXXXXX";
    char error_buffer[CAPTURE_ERROR_BUFFER_SIZE] = {0};

    capture_t *capture = NULL;
    capture_link_type_t link_type = CAPTURE_LINK_TYPE_UNKNOWN;

    int create_error;
    int open_error;
    int link_type_error;
    int null_output_error;
    int remove_error;
    bool error_string_available;

    create_error = create_empty_ethernet_pcap(pcap_path);

    TEST_CHECK(create_error == 0);

    open_error = capture_open_offline(
        pcap_path,
        &capture,
        error_buffer,
        sizeof(error_buffer)
    );

    /*
     * 只有打开成功后才能调用需要有效句柄的查询函数。
     */
    if (open_error == 0) {
        link_type_error =
            capture_get_link_type(capture, &link_type);

        null_output_error =
            capture_get_link_type(capture, NULL);

        error_string_available =
            capture_get_error(capture) != NULL;
    } else {
        link_type_error = EINVAL;
        null_output_error = EINVAL;
        error_string_available = false;
    }

    /*
     * 在执行断言前先释放资源。
     *
     * 即使后面的验证失败，测试创建的句柄和临时文件也已经清理。
     */
    capture_close(&capture);
    remove_error = remove(pcap_path);

    TEST_CHECK(open_error == 0);
    TEST_CHECK(error_buffer[0] == '\0');
    TEST_CHECK(link_type_error == 0);
    TEST_CHECK(link_type == CAPTURE_LINK_TYPE_ETHERNET);
    TEST_CHECK(null_output_error == EINVAL);
    TEST_CHECK(error_string_available);
    TEST_CHECK(capture == NULL);
    TEST_CHECK(remove_error == 0);

    return EXIT_SUCCESS;
}

int main(void)
{
    if (test_capture_open_error_handling() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] capture open error handling\n");

    if (test_open_empty_ethernet_capture() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] open empty Ethernet capture\n");

    return EXIT_SUCCESS;
}