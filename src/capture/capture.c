/*
 * libpcap在Linux上使用u_int、u_short和u_char等BSD兼容类型。
 *
 * 项目使用严格C11模式，glibc默认可能隐藏这些扩展类型。
 * _DEFAULT_SOURCE要求glibc公开默认的Linux/BSD兼容接口。
 *
 * 特性测试宏必须出现在所有系统头文件之前。
 */
#define _DEFAULT_SOURCE

#include "analyzer/capture.h"

#include <errno.h>
#include <pcap/pcap.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief 采集模块的内部对象。
 *
 * 这个结构体只在capture.c中定义，因此其他模块无法直接操作
 * native_handle，必须通过公开接口访问。
 */
struct capture {
    /**
     * libpcap原生采集句柄。
     *
     * 由pcap_open_offline创建，由pcap_close释放。
     */
    pcap_t *native_handle;

    /**
     * 转换后的项目内部链路层类型。
     */
    capture_link_type_t link_type;
};

/**
 * @brief 把错误信息安全写入调用者提供的缓冲区。
 *
 * snprintf会根据buffer_size限制写入长度，并在buffer_size大于0时
 * 保证字符串以空字符结尾。
 */
static void capture_copy_error(char *buffer,
                               size_t buffer_size,
                               const char *message)
{
    if (buffer == NULL || buffer_size == 0U) {
        return;
    }

    if (message == NULL) {
        message = "";
    }

    (void)snprintf(buffer, buffer_size, "%s", message);
}

/**
 * @brief 把libpcap的原生链路层类型转换为项目内部类型。
 */
static capture_link_type_t capture_map_link_type(int native_link_type)
{
    if (native_link_type == DLT_EN10MB) {
        /*
         * DLT_EN10MB是libpcap对普通Ethernet的名称。
         *
         * 名称中的10MB来自历史上的10 Mb/s Ethernet，但它同样用于
         * 当前常见的百兆、千兆和更高速Ethernet抓包文件。
         */
        return CAPTURE_LINK_TYPE_ETHERNET;
    }

    return CAPTURE_LINK_TYPE_UNSUPPORTED;
}

int capture_open_offline(const char *file_path,
                         capture_t **capture,
                         char *error_buffer,
                         size_t error_buffer_size)
{
    char native_error[PCAP_ERRBUF_SIZE] = {0};
    capture_t *new_capture;
    int native_link_type;

    /*
     * 错误缓冲区及其长度必须成对出现。
     */
    if ((error_buffer == NULL && error_buffer_size != 0U) ||
        (error_buffer != NULL && error_buffer_size == 0U)) {
        return EINVAL;
    }

    /*
     * 如果调用者提供了错误缓冲区，先把它初始化为空字符串。
     */
    capture_copy_error(error_buffer, error_buffer_size, "");

    if (file_path == NULL || file_path[0] == '\0' || capture == NULL) {
        capture_copy_error(error_buffer,
                           error_buffer_size,
                           "invalid capture open arguments");

        return EINVAL;
    }

    /*
     * capture_t是不透明动态对象，因此由模块负责分配和释放。
     *
     * calloc会把成员初始化为0，避免未初始化指针。
     */
    new_capture = calloc(1U, sizeof(*new_capture));

    if (new_capture == NULL) {
        capture_copy_error(error_buffer,
                           error_buffer_size,
                           "failed to allocate capture object");

        return ENOMEM;
    }

    /*
     * pcap_open_offline打开已有的PCAP文件。
     *
     * 失败时返回NULL，并把可读错误说明写入native_error。
     */
    new_capture->native_handle = pcap_open_offline(file_path, native_error);

    if (new_capture->native_handle == NULL) {
        capture_copy_error(error_buffer,
                           error_buffer_size,
                           native_error);

        free(new_capture);
        return EIO;
    }

    /*
     * PCAP文件会记录其链路层类型，不能假定所有文件都是Ethernet。
     */
    native_link_type = pcap_datalink(new_capture->native_handle);

    if (native_link_type < 0) {
        capture_copy_error(
            error_buffer,
            error_buffer_size,
            pcap_geterr(new_capture->native_handle)
        );

        pcap_close(new_capture->native_handle);
        free(new_capture);

        return EIO;
    }

    new_capture->link_type =
        capture_map_link_type(native_link_type);

    /*
     * 所有初始化步骤成功后，才把对象地址交给调用者。
     *
     * 这能保证函数失败时不会发布半初始化对象。
     */
    *capture = new_capture;

    return 0;
}

int capture_get_link_type(const capture_t *capture,
                          capture_link_type_t *link_type)
{
    if (capture == NULL || capture->native_handle == NULL || link_type == NULL) {
        return EINVAL;
    }

    *link_type = capture->link_type;

    return 0;
}

const char *capture_get_error(const capture_t *capture)
{
    if (capture == NULL || capture->native_handle == NULL) {
        return "invalid capture handle";
    }

    return pcap_geterr(capture->native_handle);
}

void capture_close(capture_t **capture)
{
    capture_t *object;

    if (capture == NULL || *capture == NULL) {
        return;
    }

    object = *capture;

    if (object->native_handle != NULL) {
        pcap_close(object->native_handle);
        object->native_handle = NULL;
    }

    free(object);

    /*
     * free只释放对象，不会自动修改调用者保存的地址。
     *
     * 手动设置为NULL可以避免调用者继续持有悬空指针。
     */
    *capture = NULL;
}