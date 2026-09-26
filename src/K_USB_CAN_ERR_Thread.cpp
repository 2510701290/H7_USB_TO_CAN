#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/logging/log.h>
#include "USB.h"
#include <zephyr/drivers/can.h>
#include <zephyr/devicetree.h> 

LOG_MODULE_REGISTER(usb_can_err, LOG_LEVEL_INF);

void USB_CAN_ERR_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    
    uint8_t err_channel = 0;

    static int64_t last_reset_time = 0;
    last_reset_time = k_uptime_get();
    while (1) 
	{
        if (k_msgq_get(&CAN_ERR_MSGQ, &err_channel, K_FOREVER) != 0) 
            continue;

        LOG_ERR("CAN error on channel %u", err_channel);

        int64_t now_time = k_uptime_get();
        if ((now_time - last_reset_time) < 1000)
            continue;
        else
        {
            int err;
            err = can_stop(can_channels[err_channel]);
            if (err != 0 && err != -EALREADY)
            {
                LOG_ERR("CH%u can_stop failed (%d)", err_channel, err);
                k_sleep(K_MSEC(50));
                continue;
            }
            err = can_start(can_channels[err_channel]);

            if (err != 0) 
            {
                LOG_ERR("CH%u can_start failed (%d)", err_channel, err);
                continue;
            }
            else 
                LOG_INF("CH%u CAN re-initialized", err_channel);
            k_sleep(K_MSEC(100));
        }
        last_reset_time = now_time;
    }
}