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

#endif