#ifndef BMS_CONTROL_IO_H
#define BMS_CONTROL_IO_H

#include "bms_control_session.h"
#include "bms_tcp_io.h"

#include <stddef.h>
#include <stdint.h>

#define BMS_CONTROL_TX_CAPACITY 4096U
#define BMS_CONTROL_REPLY_RESERVE 64U

/* 仅保存尚未交给Socket的字节；由调用者持有并先初始化。 */
typedef struct {
    unsigned char data[BMS_CONTROL_TX_CAPACITY];
    size_t offset;
    size_t length;
} bms_control_tx_t;

void bms_control_tx_init(bms_control_tx_t *tx);

/*
 * 复制一行正文并追加单个LF，不附NUL；限制同BMS_TCP_MAX_LINE。
 * 返回0成功、-1失败；ENOBUFS时原缓存不变。
 */
int bms_control_tx_enqueue(bms_control_tx_t *tx,
                           const char *body, size_t length);

/*
 * 借用Socket，每次最多一次非阻塞send，不poll、不等待、不关闭fd。
 * 返回1有进展、0为空/EAGAIN/EINTR、-1致命错误。
 * 部分发送只推进offset；失败后结束连接，不从头重发整行。
 */
int bms_control_tx_flush(bms_control_tx_t *tx, int socket_fd);

/* 会话错误回调：context指向tx，仅将ERR行复制到有界缓存。 */
int bms_control_tx_error(uint32_t id, const char *reason, void *context);

/*
 * 借用已配置且O_NONBLOCK的UART；每次最多一次单字节write。
 * 内部读取CLOCK_MONOTONIC并通过peek清理过期命令。
 * 返回1：write返回1、命令出队且ACK入缓存。
 * 返回0：无命令，或EAGAIN/EINTR未接受字节，原期限不变。
 * 返回-1：调用者结束会话；本函数清空未写队列，不隐式STOP。
 *
 * 实际写失败时尽力缓存ERR SERIAL_WRITE；不保证对端收到。
 * 调用者可在剩余总预算内有界排出错误，然后关闭连接。
 * 单线程调用；期间不允许其他代码修改session/tx。
 * 不打开/配置/关闭串口，不改变DTR/RTS/HUPCL。
 */
int bms_control_uart_step(bms_control_session_t *session,
                          bms_control_tx_t *tx, int serial_fd);

#endif