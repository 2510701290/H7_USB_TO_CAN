#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/logging/log.h>
#include "Basic_USB.h"
#include <zephyr/drivers/can.h>
#include <zephyr/devicetree.h> 

K_MSGQ_DEFINE(CAN_ERR_MSGQ, sizeof(uint8_t), 2, 1);

LOG_MODULE_REGISTER(usb_can, LOG_LEVEL_INF);

/* ==================================================================
 * USB 设备栈对象：整个工程只在这里定义一次
 *   USBD_DEVICE_DEFINE / USBD_DESC_*_DEFINE 展开为 static 变量，
 *   若写在头文件里会在每个 TU 里各生成一份（曾经的 5 份副本问题）。
 * ================================================================== */
USBD_DEVICE_DEFINE(usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 0x1D50, 0x606F);

USBD_DESC_LANG_DEFINE(lang);
USBD_DESC_MANUFACTURER_DEFINE(mfr, "A_Shui");
USBD_DESC_PRODUCT_DEFINE(product, "product_0");
USBD_DESC_SERIAL_NUMBER_DEFINE(sn);

USBD_DESC_CONFIG_DEFINE(fs_config_desc, "Full-Speed Configuration");
USBD_CONFIGURATION_DEFINE(fs_config, 0, 250, &fs_config_desc);

/* BOS 能力：USB2.0 Extension(LPM) */
static const struct usb_bos_capability_lpm bos_cap_lpm = {
    .bLength = sizeof(struct usb_bos_capability_lpm),   //字节数
    .bDescriptorType = USB_DESC_DEVICE_CAPABILITY,      //Device Capability 描述符
    .bDevCapabilityType = USB_BOS_CAPABILITY_EXTENSION, //USB2.0拓展，如果windows免驱可能要更改
    // bit0：必须为0
    // bit1：支持LPM             =0为不支持
    // bit2：BESL有效            =0不提供BESL参数
    // bit3：baseline BESL有效   =0为不支持
    // bits4–7：为baseline BESL值（单位µs）
    // bit8：deep BESL 数值有效
    // bits：Deep BESL value值（单位µs）
    .bmAttributes = 0UL,
};
USBD_DESC_BOS_DEFINE(bos_lpm, sizeof(bos_cap_lpm), &bos_cap_lpm);

struct usbd_context *usb_main_get(void)
{
    return &usbd;
}

/* ==================== ops 事件 ==================== */
static atomic_t evt_err_off;
atomic_t USB_CHANNEL_STARTED[CONFIG_USBD_GS_USB_MAX_CHANNELS] = {0},
         USB_CHANNEL_ERROR_OFF[CONFIG_USBD_GS_USB_MAX_CHANNELS] = {0},
         USB_CHANNEL_ERROR_ON[CONFIG_USBD_GS_USB_MAX_CHANNELS] = {0},
         USB_CHANNEL_ACTIVITY_RX[CONFIG_USBD_GS_USB_MAX_CHANNELS] = {0},
         USB_CHANNEL_ACTIVITY_TX[CONFIG_USBD_GS_USB_MAX_CHANNELS] = {0};

const struct device *can_channels[CONFIG_USBD_GS_USB_MAX_CHANNELS] =
{
    DEVICE_DT_GET(DT_NODELABEL(can_loopback0)),
    DEVICE_DT_GET(DT_NODELABEL(fdcan2)),
};

const struct device *gs_usb_dev = DEVICE_DT_GET(DT_NODELABEL(gs_usb0));
struct gs_usb_ops ops;

int can_channels_validate(const struct device **channels)
{
    for (int i = 0; i < CAN_CHANNELS_NUM; i++) {
        if (can_channels[i] == NULL) 
        {
            return -EINVAL;
        }
    }
    return 0;
}

int gs_usb_event_handler(const struct device *dev, uint16_t ch,
                                enum gs_usb_event event, void *user_data)
{
    switch (event)
    {
        case GS_USB_EVENT_CHANNEL_STARTED:
            atomic_inc(&USB_CHANNEL_STARTED[ch]);break;
        case GS_USB_EVENT_CHANNEL_ERROR_OFF:
            atomic_inc(&USB_CHANNEL_ERROR_OFF[ch]);break;
        case GS_USB_EVENT_CHANNEL_ERROR_ON:
            atomic_inc(&USB_CHANNEL_ERROR_ON[ch]);break;
        case GS_USB_EVENT_CHANNEL_ACTIVITY_RX:
            atomic_inc(&USB_CHANNEL_ACTIVITY_RX[ch]);break;
        case GS_USB_EVENT_CHANNEL_ACTIVITY_TX:
            atomic_inc(&USB_CHANNEL_ACTIVITY_TX[ch]);break;
        default:
            break;
    }
    // 错误计数>=128
    // if(event == GS_USB_EVENT_CHANNEL_ERROR_ON)
    // {
    //     uint8_t ch_8 = (uint8_t)ch;
    //     k_msgq_put(&CAN_ERR_MSGQ, &ch_8, K_NO_WAIT);
    //     return -1;
    // }
    return 0;
}

/* ==================================================================
 * USB 设备栈初始化：从原 main() 搬过来，顺序保持不变
 * ================================================================== */
int Basic_USB_Init(void)
{
    int err;

    /* 自检：确认上下文就是唯一那份、描述符指针非空、尚未初始化 */
    LOG_INF("usbd=%p fs_desc=%p hs_desc=%p initialized=%d",
            (void *)&usbd, usbd.fs_desc, usbd.hs_desc,
            usbd.status.initialized);

    ops.event = gs_usb_event_handler;

    err = can_channels_validate(can_channels);
    if (err != 0)
    {
        LOG_ERR("can_channels_validate failed (err %d)", err);
        return err;
    }

    err = gs_usb_register(gs_usb_dev, can_channels, ARRAY_SIZE(can_channels), &ops, NULL);
    if (err != 0)
    {
        LOG_ERR("failed to register gs_usb (err %d)", err);
        return err;
    }

    if (!device_is_ready(gs_usb_dev))
    {
        LOG_ERR("gs_usb not ready");
        return -ENODEV;
    }

    /* ---------- 描述符 / 配置 / 类注册 ---------- */
    err = usbd_add_descriptor(&usbd, &lang);
    if (err != 0)
    {
        LOG_ERR("failed to add language descriptor (err %d)", err);
        return err;
    }

    err = usbd_add_descriptor(&usbd, &mfr);
    if (err != 0)
    {
        LOG_ERR("failed to add manufacturer descriptor (err %d)", err);
        return err;
    }

    err = usbd_add_descriptor(&usbd, &product);
    if (err != 0)
    {
        LOG_ERR("failed to add product descriptor (err %d)", err);
        return err;
    }

    err = usbd_add_descriptor(&usbd, &sn);
    if (err != 0)
    {
        LOG_ERR("failed to add serial number descriptor (err %d)", err);
        return err;
    }

    err = usbd_add_configuration(&usbd, USBD_SPEED_FS, &fs_config);
    if (err != 0)
    {
        LOG_ERR("failed to add full-speed configuration (err %d)", err);
        return err;
    }

    err = usbd_register_class(&usbd, "gs_usb_0", USBD_SPEED_FS, 1);
    if (err != 0)
    {
        LOG_ERR("failed to register gs_usb class (err %d)", err);
        return err;
    }

    err = usbd_device_set_code_triple(&usbd, USBD_SPEED_FS, 0, 0, 0);
    if (err != 0)
    {
        LOG_ERR("failed to set code triple (err %d)", err);
        return err;
    }

    /* Set USB version to 2.0.1 to trigger BOS descriptor read (required for WinUSB) */
    err = usbd_device_set_bcd_usb(&usbd, USBD_SPEED_FS, USB_SRN_2_0_1);
    if (err != 0)
    {
        LOG_ERR("failed to set FS bcdUSB (err %d)", err);
        return err;
    }

    /* Set device version */
    err = usbd_device_set_bcd_device(&usbd, APP_VERSION_BCD);
    if (err != 0)
    {
        LOG_ERR("failed to set bcdDevice (err %d)", err);
        return err;
    }

    err = usbd_add_descriptor(&usbd, &bos_lpm);
    if (err != 0)
    {
        LOG_ERR("failed to add BOS LPM descriptor (err %d)", err);
        return err;
    }

    /* ---------- 启动协议栈 ---------- */
    err = usbd_init(&usbd);
    if (err != 0)
    {
        LOG_ERR("failed to initialize USB device (err %d)", err);
        return err;
    }

    err = usbd_enable(&usbd);
    if (err != 0)
    {
        LOG_ERR("failed to enable USB device (err %d)", err);
        return err;
    }

    LOG_INF("USB device stack initialized (%04x:%04x)", 0x1D50, 0x606F);
    return 0;
}