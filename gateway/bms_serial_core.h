#ifndef BMS_SERIAL_CORE_H
#define BMS_SERIAL_CORE_H

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int (*bms_serial_line_sink_fn)(
    const char *line, size_t length, void *context
);

typedef struct {
    uint64_t lines;
    uint64_t discarded;
    size_t tail_bytes;
    bool discarding;
} bms_serial_stats_t;

/* 借用已打开的串口fd，配置115200/8N1及DTR/RTS；不负责打开或关闭。 */
int bms_serial_configure_fd(int fd);

/*
 * 借用非阻塞输入fd；只读输入，不调用串口配置、不关闭fd。
 * 每次调用从启动片段丢弃状态开始，输出完整行并填写stats。
 * line只在回调期间有效；context、stop_flag和stats由调用者拥有。
 * stop_flag只由调用者/信号处理函数置位，本函数不清零。
 * 返回0：接收时限到达或观察到停止标志；返回-1：errno说明错误。
 * timeout_ms约束接收循环等待，不能强制中断一个阻塞的输出回调。
 */
int bms_serial_receive_fd(int fd,
                          bms_serial_line_sink_fn sink,
                          void *context,
                          int timeout_ms,
                          const volatile sig_atomic_t *stop_flag,
                          bms_serial_stats_t *stats);

#define BMS_SERIAL_LINE_CAPACITY 1024U

typedef struct {
    char data[BMS_SERIAL_LINE_CAPACITY + 1U];
    size_t used;
    bool discarding;
    uint64_t lines;
    uint64_t discarded;
} bms_serial_framer_t;

/* 启动时丢弃首个残片至LF；不执行I/O，调用者拥有framer。 */
void bms_serial_framer_init(bms_serial_framer_t *framer);

/* 借用输入块与回调参数，复用原行整理规则，不保存输入指针。 */
int bms_serial_framer_feed(bms_serial_framer_t *framer,
                           const unsigned char *data, size_t length,
                           bms_serial_line_sink_fn sink, void *context);

/* lines表示回调成功接收的完整行数，不代表远端已处理。 */
void bms_serial_framer_stats(const bms_serial_framer_t *framer,
                             bms_serial_stats_t *stats);

#endif