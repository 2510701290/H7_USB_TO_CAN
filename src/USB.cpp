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

const struct device *can_channels[] =
{
    DEVICE_DT_GET(DT_NODELABEL(can_loopback0)),
    DEVICE_DT_GET(DT_NODELABEL(fdcan2)),
};

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
    ARG_UNUSED(dev);
    ARG_UNUSED(ch);
    ARG_UNUSED(user_data);

    // 错误计数>=128
    if(event == GS_USB_EVENT_CHANNEL_ERROR_ON)
    {
        uint8_t ch_8 = (uint8_t)ch;
        k_msgq_put(&CAN_ERR_MSGQ, &ch_8, K_NO_WAIT);
        return -1;
    }
    return 0;
}