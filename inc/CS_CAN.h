/*
 * CS_CAN - CAN 监视 / 查错
 *
 * 与 Basic_CAN 的正常收发逻辑解耦：Basic_CAN 只负责 msgq 与 start/stop，
 * 所有调试打印（状态变化、错误计数、帧内容）都在这里。
 */
#ifndef CS_CAN_H_
#define CS_CAN_H_

#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <zephyr/devicetree.h>

/* 注册 fdcan1 / fdcan3 的状态变化回调：
 * ERROR-WARNING / ERROR-PASSIVE / BUS-OFF 及错误计数会打印到 RTT。
 * 可在 USB(gs_usb) 模式和独立 CAN 模式下使用，多次调用无副作用。 */
void CS_CAN_Monitor_Init(void);

/* 打印一帧 CAN/CAN-FD 报文，例如：
 *   CS_CAN_Log_Frame("fdcan1 RX", frame);
 * 可在任意 RX 回调里调用（中断上下文安全，使用 deferred logging）。 */
void CS_CAN_Log_Frame(const char *tag, const struct can_frame *frame);

/* 启动丢包监测线程：每 1 秒打印一次
 *   - 控制器 RX FIFO 溢出增量（真正意义上的接收丢帧）
 *   - ACK / CRC / 格式 / 位错误增量（发送失败分类）
 *   - gs_usb 每秒实际送到主机的 RX/TX 帧数（USB 模式才有意义）
 * 重复调用无副作用。需要 CONFIG_CAN_STATS=y。 */
void CS_CAN_Loss_Monitor_Start(void);

#endif /* CS_CAN_H_ */
