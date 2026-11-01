/*
 * 主机侧白盒测试：只使用临时文件和匿名管道，不打开真实串口。
 * 临时改名原main，直接复用原文件内的static函数，不复制生产逻辑。
 * 不调用bms_serial_receiver_main或configure_serial，不修改DTR/RTS检查。
 */
#define main bms_serial_receiver_main
#include "../../gateway/bms_serial_recv.c"
#undef main

#include <string.h>

/* 不用assert，避免NDEBUG让测试检查消失。失败立即结束测试进程。 */
#define TEST_CHECK(condition)                                           \
    do {                                                                \
        if (!(condition)) {                                             \
            fprintf(stderr, "[FAIL] %s:%d: %s\n",                        \
                    __FILE__, __LINE__, #condition);                     \
            exit(EXIT_FAILURE);                                         \
        }                                                               \
    } while (false)

typedef struct {
    FILE *stream;
    int saved_stdout;
} output_capture_t;

/* 借用stdout：保存其原目标，把被测函数的puts输出导向临时文件。 */
static output_capture_t capture_begin(void)
{
    output_capture_t capture;

    TEST_CHECK(fflush(stdout) == 0);

    capture.stream = tmpfile();
    TEST_CHECK(capture.stream != NULL);

    capture.saved_stdout = dup(STDOUT_FILENO);
    TEST_CHECK(capture.saved_stdout >= 0);
    TEST_CHECK(dup2(fileno(capture.stream), STDOUT_FILENO) == STDOUT_FILENO);

    return capture;
}

/* 先恢复stdout，再逐字节核对输出，最后关闭本次捕获拥有的资源。 */
static void capture_expect(output_capture_t *capture, const char *expected)
{
    unsigned char actual[LINE_CAPACITY * 2U + 64U];
    size_t expected_size = strlen(expected);
    size_t actual_size;

    TEST_CHECK(fflush(stdout) == 0);
    TEST_CHECK(dup2(capture->saved_stdout, STDOUT_FILENO) == STDOUT_FILENO);
    TEST_CHECK(close(capture->saved_stdout) == 0);

    TEST_CHECK(fseek(capture->stream, 0L, SEEK_SET) == 0);
    actual_size = fread(actual, 1U, sizeof(actual), capture->stream);

    TEST_CHECK(ferror(capture->stream) == 0);
    TEST_CHECK(expected_size <= sizeof(actual));
    TEST_CHECK(actual_size == expected_size);
    TEST_CHECK(memcmp(actual, expected, expected_size) == 0);
    TEST_CHECK(fgetc(capture->stream) == EOF);
    TEST_CHECK(fclose(capture->stream) == 0);
}

/* 只用于普通测试字符串；包含NUL的数据要直接传feed_bytes及显式长度。 */
static void feed_text(line_buffer_t *buffer, const char *text)
{
    TEST_CHECK(feed_bytes(buffer,
                          (const unsigned char *)text,
                          strlen(text),
                          stdout_line_sink, NULL) == 0);
}

static void test_fragments_and_multiple_lines(void)
{
    line_buffer_t buffer = {.discarding = true};
    output_capture_t capture = capture_begin();

    /* 启动片段不进入输出；直到首个LF才开始接收完整行。 */
    feed_text(&buffer, "startup fragment");
    TEST_CHECK(buffer.lines == 0U);
    TEST_CHECK(buffer.used == 0U);

    feed_text(&buffer, "\nBMS sample=");
    TEST_CHECK(buffer.discarded == 1U);
    TEST_CHECK(buffer.lines == 0U);

    /* CR和LF分在两次读取中，也应该只生成一条完整行。 */
    feed_text(&buffer, "1\r");
    TEST_CHECK(buffer.lines == 0U);

    feed_text(&buffer, "\nBMS sample=2\nBMS sample=3\r\npending");

    TEST_CHECK(buffer.lines == 3U);
    TEST_CHECK(buffer.discarded == 1U);
    TEST_CHECK(buffer.used == strlen("pending"));
    TEST_CHECK(!buffer.discarding);

    /* pending没有换行，必须留在缓存，不能出现在输出文件。 */
    capture_expect(&capture,
                   "BMS sample=1\nBMS sample=2\nBMS sample=3\n");
}

static void test_capacity_and_resynchronization(void)
{
    line_buffer_t buffer = {0};
    output_capture_t capture = capture_begin();
    unsigned char exact[LINE_CAPACITY + 1U];
    unsigned char oversized[LINE_CAPACITY + 2U];
    char expected[LINE_CAPACITY + 32U];

    /* 1024个正文字符加LF：应当完整接收。 */
    memset(exact, 'A', sizeof(exact));
    exact[LINE_CAPACITY] = '\n';
    TEST_CHECK(feed_bytes(
    &buffer, exact, sizeof(exact), stdout_line_sink, NULL
) == 0);

    /* 1025个正文字符加LF：整行舍弃，下一条正常行仍可接收。 */
    memset(oversized, 'B', sizeof(oversized));
    oversized[sizeof(oversized) - 1U] = '\n';
    TEST_CHECK(feed_bytes(
    &buffer, oversized, sizeof(oversized), stdout_line_sink, NULL
) == 0);
    feed_text(&buffer, "BMS recovered\n");

    TEST_CHECK(buffer.lines == 2U);
    TEST_CHECK(buffer.discarded == 1U);
    TEST_CHECK(buffer.used == 0U);
    TEST_CHECK(!buffer.discarding);

    memset(expected, 'A', LINE_CAPACITY);
    strcpy(expected + LINE_CAPACITY, "\nBMS recovered\n");
    capture_expect(&capture, expected);
}

static void test_control_bytes_and_empty_lines(void)
{
    line_buffer_t buffer = {0};
    output_capture_t capture = capture_begin();
    const unsigned char invalid[] = {
        'x', '\0', 'y', '\n',
        'x', 0x1bU, 'y', '\n',
        'x', 0x7fU, 'y', '\n'
    };

    /* 分别含NUL、ESC、DEL的三行都应舍弃。 */
    TEST_CHECK(feed_bytes(
    &buffer, invalid, sizeof(invalid), stdout_line_sink, NULL
) == 0);

    /* 空行忽略；TAB保留；行尾CR去掉。 */
    feed_text(&buffer, "\n\r\nBMS\tok\r\n");

    TEST_CHECK(buffer.lines == 1U);
    TEST_CHECK(buffer.discarded == 3U);
    TEST_CHECK(buffer.used == 0U);
    TEST_CHECK(!buffer.discarding);
    capture_expect(&capture, "BMS\tok\n");
}

static void test_pipe_hangup(bool with_data)
{
    line_buffer_t buffer = {.discarding = true};
    output_capture_t capture = capture_begin();
    const char payload[] = "startup\nBMS sample=4\r\npending";
    int channel[2];
    int flags;
    int result;
    int saved_errno;

    /* channel[0]读，channel[1]写；二者都由本测试负责关闭。 */
    TEST_CHECK(pipe(channel) == 0);
    flags = fcntl(channel[0], F_GETFL);
    TEST_CHECK(flags >= 0);
    TEST_CHECK(fcntl(channel[0], F_SETFL, flags | O_NONBLOCK) == 0);

    if (with_data) {
        TEST_CHECK(write(channel[1], payload, sizeof(payload) - 1U) ==
                   (ssize_t)(sizeof(payload) - 1U));
    }

    /* 关闭写端只影响匿名管道，不涉及USB线或STM32供电。 */
    TEST_CHECK(close(channel[1]) == 0);

    stop_requested = 0;
    result = receive_lines(
        channel[0], &buffer, stdout_line_sink, NULL
    );
    saved_errno = errno;  /* 后续close等调用可能改变errno，必须立即保存。 */

    TEST_CHECK(close(channel[0]) == 0);
    TEST_CHECK(result == -1);
    TEST_CHECK(saved_errno == ENODEV);

    if (with_data) {
        TEST_CHECK(buffer.lines == 1U);
        TEST_CHECK(buffer.discarded == 1U);
        TEST_CHECK(buffer.used == strlen("pending"));
        TEST_CHECK(!buffer.discarding);
        capture_expect(&capture, "BMS sample=4\n");
    } else {
        TEST_CHECK(buffer.lines == 0U);
        TEST_CHECK(buffer.discarded == 0U);
        TEST_CHECK(buffer.used == 0U);
        capture_expect(&capture, "");
    }
}

/*
 * 测试回调自己的缓存：复制行内容，不保存接收缓存的借用指针。
 * fail_on=0不注入失败；其他值表示第几次调用返回EPIPE。
 */
typedef struct {
    char text[64];
    size_t used;
    size_t calls;
    size_t fail_on;
} sink_probe_t;

static int probe_line_sink(const char *line, size_t length, void *context)
{
    sink_probe_t *probe = context;

    ++probe->calls;
    TEST_CHECK(line[length] == '\0');
    TEST_CHECK(memchr(line, '\n', length) == NULL);

    if (probe->calls == probe->fail_on) {
        errno = EPIPE;
        return -1;
    }

    TEST_CHECK(length + 2U <= sizeof(probe->text) - probe->used);
    memcpy(probe->text + probe->used, line, length);
    probe->used += length;
    probe->text[probe->used++] = '\n';
    probe->text[probe->used] = '\0';
    return 0;
}

static void test_callback_delivery(void)
{
    line_buffer_t buffer = {.discarding = true};
    sink_probe_t probe = {0};
    const unsigned char input[] =
        "startup\nBMS one\r\nBMS two\npending";

    TEST_CHECK(feed_bytes(
        &buffer, input, sizeof(input) - 1U, probe_line_sink, &probe
    ) == 0);
    TEST_CHECK(probe.calls == 2U);
    TEST_CHECK(strcmp(probe.text, "BMS one\nBMS two\n") == 0);
    TEST_CHECK(buffer.lines == 2U);
    TEST_CHECK(buffer.discarded == 1U);
    TEST_CHECK(buffer.used == strlen("pending"));
}

static void test_callback_failure(void)
{
    line_buffer_t buffer = {0};
    sink_probe_t probe = {.fail_on = 2U};
    const unsigned char input[] = "first\nsecond\nthird\n";

    TEST_CHECK(feed_bytes(
        &buffer, input, sizeof(input) - 1U, probe_line_sink, &probe
    ) == -1);
    TEST_CHECK(errno == EPIPE);
    TEST_CHECK(probe.calls == 2U);
    TEST_CHECK(strcmp(probe.text, "first\n") == 0);
    TEST_CHECK(buffer.lines == 1U);

    TEST_CHECK(feed_bytes(
        &buffer, input, sizeof(input) - 1U, NULL, NULL
    ) == -1);
    TEST_CHECK(errno == EINVAL);
}

int main(void)
{
    test_callback_delivery();
    fprintf(stderr, "[PASS] complete lines delivered through callback\n");

    test_callback_failure();
    fprintf(stderr, "[PASS] callback failure propagation and stop\n");

    test_fragments_and_multiple_lines();
    fprintf(stderr, "[PASS] fragments, CRLF, multiple lines and tail\n");

    test_capacity_and_resynchronization();
    fprintf(stderr, "[PASS] line capacity and resynchronization\n");

    test_control_bytes_and_empty_lines();
    fprintf(stderr, "[PASS] control bytes and empty lines\n");

    test_pipe_hangup(true);
    fprintf(stderr, "[PASS] pipe hangup with data\n");

    test_pipe_hangup(false);
    fprintf(stderr, "[PASS] pipe hangup without data\n");

    fprintf(stderr, "[PASS] BMS serial host logic tests\n");
    return EXIT_SUCCESS;
}