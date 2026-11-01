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

#endif