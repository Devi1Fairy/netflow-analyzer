#ifndef BMS_TCP_IO_H
#define BMS_TCP_IO_H

#include <stddef.h>

#define BMS_TCP_MAX_LINE 1024U

/*
 * 向已连接的流式Socket发送一行：line不包含LF，本函数追加LF。
 * length为正且不超过1024，不包含NUL或LF；timeout_ms为正的总时间预算。
 *
 * 返回0：全部字节已交给本机Socket；不等于远端应用已经处理。
 * 返回-1：查看errno；部分前缀可能已发送，调用者应关闭连接，不整行重发。
 * 信号中断返回EINTR，由上层决定退出。
 *
 * 借用socket_fd和line，不关闭Socket、不修改输入或描述符的阻塞标志。
 * 这是有界同步发送：等待期间上层不会继续处理其他输入。
 */
int bms_tcp_send_line(int socket_fd,
                      const char *line,
                      size_t length,
                      int timeout_ms);

/*
 * 单条连接的输出配置；socket_fd由调用者拥有，本结构不负责关闭。
 * timeout_ms是每行发送预算，不是整次接收的总时间上限。
 */
typedef struct {
    int socket_fd;
    int timeout_ms;
} bms_tcp_sink_t;

/*
 * 适配完整行回调接口。context必须指向有效的bms_tcp_sink_t。
 * 借用line和context，不保存指针、不修改配置，也不关闭Socket。
 * 返回0表示本机Socket接受整行；返回-1时保留发送错误。
 * 失败后调用者应结束该连接，不在同一字节流中整行重试。
 */
int bms_tcp_line_sink(const char *line, size_t length, void *context);

#endif