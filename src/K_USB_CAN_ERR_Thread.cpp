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

    
    atomic_t USB_T_STARTED[CONFIG_USBD_GS_USB_MAX_CHANNELS],
             USB_T_ERROR_OFF[CONFIG_USBD_GS_USB_MAX_CHANNELS],
             USB_T_ERROR_ON[CONFIG_USBD_GS_USB_MAX_CHANNELS],
             USB_T_ACTIVITY_RX[CONFIG_USBD_GS_USB_MAX_CHANNELS],
             USB_T_ACTIVITY_TX[CONFIG_USBD_GS_USB_MAX_CHANNELS];

    while (1) 
	{
        for(uint8_t i = 0; i < CONFIG_USBD_GS_USB_MAX_CHANNELS;i++)
        {
            if( USB_T_ACTIVITY_RX[i] == USB_CHANNEL_ACTIVITY_RX[i] && 
                USB_T_ACTIVITY_TX[i] == USB_CHANNEL_ACTIVITY_TX[i])
                LOG_INF("CAN Channel[%d] is not working!",i);

            USB_T_STARTED[i] = atomic_clear(&USB_CHANNEL_STARTED[i]);
            USB_T_ERROR_OFF[i] = atomic_clear(&USB_CHANNEL_ERROR_OFF[i]);
            USB_T_ERROR_ON[i] = atomic_clear(&USB_CHANNEL_ERROR_ON[i]);
            USB_T_ACTIVITY_RX[i] = atomic_clear(&USB_CHANNEL_ACTIVITY_RX[i]);
            USB_T_ACTIVITY_TX[i] = atomic_clear(&USB_CHANNEL_ACTIVITY_TX[i]);

        }
        k_sleep(K_SECONDS(5));
        
    }
}