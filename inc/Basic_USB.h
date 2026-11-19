#pragma once

#ifdef CONFIG_USB_DEVICE_STACK_NEXT
#include <zephyr/usb/usbd.h>
#include <cannectivity/usb/class/gs_usb.h>

#define CAN_CHANNELS_NUM CONFIG_USBD_GS_USB_MAX_CHANNELS
#define APP_VERSION_BCD  0x0100

/* ------------------------------------------------------------------
 * 注意：下面这些 USBD_*_DEFINE 宏展开的是 static 对象，只能在
 * src/Basic_USB.cpp 里定义一次，绝不能写在头文件里。
 * 这里只做函数声明。
 * ------------------------------------------------------------------ */
struct usbd_context *usb_main_get(void);

/* 建描述符/配置/注册类 → usbd_init() → usbd_enable()
 * 前置条件：gs_usb_register() 已成功返回 */
int usb_device_setup(void);

extern atomic_t USB_CHANNEL_STARTED[CONFIG_USBD_GS_USB_MAX_CHANNELS],
                USB_CHANNEL_ERROR_OFF[CONFIG_USBD_GS_USB_MAX_CHANNELS],
                USB_CHANNEL_ERROR_ON[CONFIG_USBD_GS_USB_MAX_CHANNELS],
                USB_CHANNEL_ACTIVITY_RX[CONFIG_USBD_GS_USB_MAX_CHANNELS],
                USB_CHANNEL_ACTIVITY_TX[CONFIG_USBD_GS_USB_MAX_CHANNELS];

extern const struct device *can_channels[CAN_CHANNELS_NUM];
extern const struct device *gs_usb_dev;
extern struct gs_usb_ops ops;

int gs_usb_event_handler(const struct device *dev, uint16_t ch,
                         enum gs_usb_event event, void *user_data);

int can_channels_validate(const struct device **channels);

void USB_CAN_ERR_thread(void *p1, void *p2, void *p3);

#endif