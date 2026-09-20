#include <K_RTT_Thread.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/usbd_msg.h>
#include <USB.h>

LOG_MODULE_REGISTER(rtt_thread, LOG_LEVEL_INF);

int LOG_INFO = 0;
int err = 0;

/* 线程入口签名固定为 void (*)(void *, void *, void *) */
void RTT_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) 
	{
		if(err != 0)
        	LOG_ERR("%d", err);
		else
			LOG_INF("LOG_INFO: %d", LOG_INFO);
        k_sleep(K_MSEC(500));
    }
}