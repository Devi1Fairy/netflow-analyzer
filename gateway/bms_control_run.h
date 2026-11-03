#ifndef BMS_CONTROL_RUN_H
#define BMS_CONTROL_RUN_H

#include "bms_serial_core.h"

#include <signal.h>
#include <stdint.h>

/*
 * 仅在串口已配置成功后调用；借用两个非阻塞fd、停止标志和stats。
 * deadline_ms是CLOCK_MONOTONIC绝对总期限，不重新计时。
 * 返回0：到期、受控停止或观察到客户端结束；-1：errno说明错误。
 * 不打开/配置/关闭fd，不重连，不发送隐式STOP。
 * stats.lines统计进入发送缓存的遥测行，不等于Qt已收到。
 */
int bms_control_run(int serial_fd, int client_fd, int64_t deadline_ms,
                    const volatile sig_atomic_t *stop,
                    bms_serial_stats_t *stats);

#endif