#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <soc.h>

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

K_THREAD_DEFINE(CAN2_tid, 2048,
				CAN2_thread, 
				NULL, NULL, NULL, 
				1, 0, SYS_FOREVER_MS);

K_THREAD_DEFINE(CAN_loopback_tid, 2048,
				CAN_loopback_thread, 
				NULL, NULL, NULL, 
				5, 0, 0);

// K_THREAD_DEFINE(USB_CAN_Err_tid, 1024,
// 				USB_CAN_ERR_thread, 
// 				NULL, NULL, NULL, 
// 				2, 0, 0);

int main_err = 0;
static bool can2_started;

/* ------------------------------------------------------------------
 * 复位探针：__noinit 变量不被启动代码清零，可跨复位累加。
 * 若 boot # 一直在涨，就说明板子在反复复位（枚举失败的头号原因）。
 * ------------------------------------------------------------------ */
static __noinit uint32_t boot_cnt;

static void log_reset_cause(void)
{
	uint32_t rsr = RCC->RSR;

	boot_cnt++;
	LOG_WRN("boot #%u  RCC->RSR=0x%08x", boot_cnt, rsr);

#ifdef RCC_RSR_BORRSTF
	if (rsr & RCC_RSR_BORRSTF)  { LOG_WRN("  -> brown-out (BOR) reset"); }
#endif
#ifdef RCC_RSR_PORRSTF
	if (rsr & RCC_RSR_PORRSTF)  { LOG_WRN("  -> power-on (POR) reset"); }
#endif
#ifdef RCC_RSR_PINRSTF
	if (rsr & RCC_RSR_PINRSTF)  { LOG_WRN("  -> NRST pin reset"); }
#endif
#ifdef RCC_RSR_SFTRSTF
	if (rsr & RCC_RSR_SFTRSTF)  { LOG_WRN("  -> software reset"); }
#endif
#ifdef RCC_RSR_IWDGRSTF
	if (rsr & RCC_RSR_IWDGRSTF) { LOG_WRN("  -> IWDG reset"); }
#endif
#ifdef RCC_RSR_WWDGRSTF
	if (rsr & RCC_RSR_WWDGRSTF) { LOG_WRN("  -> WWDG reset"); }
#endif
#ifdef RCC_RSR_LPWRRSTF
	if (rsr & RCC_RSR_LPWRRSTF) { LOG_WRN("  -> low-power reset"); }
#endif

	/* 清标志，下次读到的才是新的复位原因 */
#if defined(RCC_RSR_RMVF)
	RCC->RSR |= RCC_RSR_RMVF;
#elif defined(RCC_RSR_RMVF_Msk)
	RCC->RSR |= RCC_RSR_RMVF_Msk;
#endif
}

int main(void)
{
	log_reset_cause();
	LOG_INF("Begin Gimbal");

	main_err = usb_device_setup();
	if (main_err != 0)
	{
		LOG_ERR("usb_device_setup failed (err %d)", main_err);
		return main_err;
	}

	/* 调试 USB 时保持 USB 模式（C_M_TO_S() 会 usbd_disable()）
	 * 正常模式把下面两段的注释去掉 */
	// M_S_Control.C_M_TO_S(usb_main_get());
	// if (!can2_started) 
	// {
	// 	can2_started = true;
	// 	k_thread_start(CAN2_tid);
	// }
	
	return 0;
}
