#pragma once

#ifdef CONFIG_USB_DEVICE_STACK_NEXT
#include <zephyr/usb/usbd.h>
#include <cannectivity/usb/class/gs_usb.h>

#define CAN_CHANNELS_NUM CONFIG_USBD_GS_USB_MAX_CHANNELS
#define APP_VERSION_BCD 0x0100

/* ==================================================================
 * 注意：USBD_DEVICE_DEFINE / USBD_DESC_*_DEFINE 展开出来全是 static
 * 变量。若直接写在头文件里，每个包含它的 .cpp 都会生成一份独立副本
 * （map 里曾经出现 5 个 _usbd_context.static.usbd_，分布在
 *  main.cpp / Basic_USB.cpp / K_RTT_Thread.cpp /
 *  K_USB_CAN_ERR_Thread.cpp / M_S_Control.cpp）。
 * 因此所有 usbd 对象统一在 src/Basic_USB.cpp 里定义一次，
 * 这里只做函数声明，其他文件一律通过函数拿指针。
 * ================================================================== */

/* 返回唯一的 usbd 上下文（定义在 Basic_USB.cpp） */
struct usbd_context *usb_main_get(void);

/* 完整初始化 USB 设备栈：
 *   gs_usb_register -> 描述符/配置/类注册 -> usbd_init() -> usbd_enable()
 * 成功返回 0，失败返回错误码（与原 main() 中的步骤一一对应）。 */
int Basic_USB_Init(void);

extern atomic_t USB_CHANNEL_STARTED[CONFIG_USBD_GS_USB_MAX_CHANNELS],
                USB_CHANNEL_ERROR_OFF[CONFIG_USBD_GS_USB_MAX_CHANNELS],
                USB_CHANNEL_ERROR_ON[CONFIG_USBD_GS_USB_MAX_CHANNELS],
                USB_CHANNEL_ACTIVITY_RX[CONFIG_USBD_GS_USB_MAX_CHANNELS],
                USB_CHANNEL_ACTIVITY_TX[CONFIG_USBD_GS_USB_MAX_CHANNELS];

extern const struct device *can_channels[CAN_CHANNELS_NUM];

/* BOS 能力描述符（bos_cap_lpm）与 BOS 节点（bos_lpm）同样定义在
 * src/Basic_USB.cpp，这里不需要可见。 */

extern const struct device *gs_usb_dev;
extern struct gs_usb_ops ops;

int gs_usb_event_handler(const struct device *dev, uint16_t ch,
                         enum gs_usb_event event, void *user_data);

int can_channels_validate(const struct device **channels);

void USB_CAN_ERR_thread(void *p1, void *p2, void *p3);

#endif