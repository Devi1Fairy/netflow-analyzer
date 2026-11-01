#ifndef BMS_TCP_SERVER_H
#define BMS_TCP_SERVER_H

#include <stdint.h>

/*
 * bind_ipv4必须是本机的数字IPv4地址；port为主机字节序。
 * port=0表示让内核选择空闲端口，主要用于测试。
 * 成功返回非阻塞、close-on-exec的监听fd，由调用者负责close。
 * 失败返回-1并设置errno。函数不保留bind_ipv4指针。
 */
int bms_tcp_listen_ipv4(const char *bind_ipv4, uint16_t port);

/*
 * 借用由上面接口创建的非阻塞监听fd，在timeout_ms预算内等待连接。
 * 成功返回新的客户端fd（非阻塞、close-on-exec），由调用者close。
 * 失败返回-1并设置errno；超时为ETIMEDOUT，信号中断为EINTR。
 * 不关闭监听fd，不修改其标志；调用期间不能由其他线程改动该fd。
 */
int bms_tcp_accept_client(int listener_fd, int timeout_ms);

#endif