#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/logging/log.h>
#include "USB.h"
#include <zephyr/drivers/can.h>
#include <zephyr/devicetree.h> 

K_MSGQ_DEFINE(CAN_ERR_MSGQ, sizeof(uint8_t), 2, 1);

LOG_MODULE_REGISTER(usb_can, LOG_LEVEL_INF);

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