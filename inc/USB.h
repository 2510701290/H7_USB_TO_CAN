#pragma once

#ifdef CONFIG_USB_DEVICE_STACK_NEXT
#include <zephyr/usb/usbd.h>
#include <cannectivity/usb/class/gs_usb.h>

#define CAN_CHANNELS_NUM CONFIG_USBD_GS_USB_MAX_CHANNELS
#define APP_VERSION_BCD 0x0100

/* ============ 1. 设备上下文 + 标准字符串/配置描述符 ============ */
USBD_DEVICE_DEFINE(usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 0x1D50, 0x606F);
USBD_DESC_LANG_DEFINE(lang);
USBD_DESC_MANUFACTURER_DEFINE(mfr, "A_Shui");
USBD_DESC_PRODUCT_DEFINE(product, "product_0");
USBD_DESC_SERIAL_NUMBER_DEFINE(sn);
USBD_DESC_CONFIG_DEFINE(fs_config_desc, "Full-Speed Configuration");
USBD_CONFIGURATION_DEFINE(fs_config, 0, 250, &fs_config_desc);

extern atomic_t USB_CHANNEL_STARTED[CONFIG_USBD_GS_USB_MAX_CHANNELS],
                USB_CHANNEL_ERROR_OFF[CONFIG_USBD_GS_USB_MAX_CHANNELS],
                USB_CHANNEL_ERROR_ON[CONFIG_USBD_GS_USB_MAX_CHANNELS],
                USB_CHANNEL_ACTIVITY_RX[CONFIG_USBD_GS_USB_MAX_CHANNELS],
                USB_CHANNEL_ACTIVITY_TX[CONFIG_USBD_GS_USB_MAX_CHANNELS];

extern const struct device *can_channels[CAN_CHANNELS_NUM];

/* ============ 2. BOS 能力：USB2.0 Extension(LPM) ============ */
static const struct usb_bos_capability_lpm bos_cap_lpm = {
    .bLength = sizeof(struct usb_bos_capability_lpm),
    .bDescriptorType = USB_DESC_DEVICE_CAPABILITY,
    .bDevCapabilityType = USB_BOS_CAPABILITY_EXTENSION,
    .bmAttributes = 0UL,
};

/* ============ 3. BOS 描述符节点 ============ */
USBD_DESC_BOS_DEFINE(bos_lpm, sizeof(bos_cap_lpm), &bos_cap_lpm);

struct usbd_context *usb_main_get(void);
extern const struct device *gs_usb_dev;
extern struct gs_usb_ops ops;

int gs_usb_event_handler(const struct device *dev, uint16_t ch,
                         enum gs_usb_event event, void *user_data);

int can_channels_validate(const struct device **channels);

void USB_CAN_ERR_thread(void *p1, void *p2, void *p3);

#endif