#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <Basic_USB.h>

#include <K_RTT_Thread.h>
#include <K_CAN_Thread.h>

#include <M_S_Control.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

K_THREAD_DEFINE(RTT_TX_tid, 4096,
				RTT_TX_thread, 
				NULL, NULL, NULL,
				7, 0, 0);

				
K_THREAD_DEFINE(RTT_RX_tid, 4096,
				RTT_RX_thread,
				NULL, NULL, NULL,
				6, 0, 0);

// K_THREAD_DEFINE(CAN2_tid, 2048,
// 				CAN2_thread, 
// 				NULL, NULL, NULL, 
// 				5, 0, 0);

K_THREAD_DEFINE(CAN_loopback_tid, 2048,
				CAN_loopback_thread, 
				NULL, NULL, NULL, 
				5, 0, 0);

// K_THREAD_DEFINE(USB_CAN_Err_tid, 1024,
// 				USB_CAN_ERR_thread, 
// 				NULL, NULL, NULL, 
// 				2, 0, 0);

/*
 * usbd 上下文由 src/Basic_USB.cpp 唯一定义，
 * 需要指针时统一用 usb_main_get()（声明在 Basic_USB.h）。
 */
int main_err = 0;

int main(void)
{
	LOG_INF("Begin Work");

#ifdef CONFIG_USB_DEVICE_STACK_NEXT
	/* gs_usb 注册 + 全部描述符/配置/类注册 + usbd_init() + usbd_enable()
	 * 都在 Basic_USB.cpp 里完成，保证 usbd 只有唯一一份实例。 */
	main_err = Basic_USB_Init();
	if (main_err != 0)
	{
		LOG_ERR("Basic_USB_Init failed (err %d)", main_err);
		return main_err;
	}
#else
	/* Old USB stack */
	main_err = usb_enable(NULL);
	if (main_err != 0)
	{
		LOG_ERR("failed to enable USB (err %d)", main_err);
		return main_err;
	}
#endif

	return 0;
}
