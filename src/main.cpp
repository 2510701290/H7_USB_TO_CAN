#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <USB.h>

#include <K_RTT_Thread.h>
#include <K_CAN_Thread.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

K_THREAD_DEFINE(RTT_tid, 4096,
				RTT_thread, 
				NULL, NULL, NULL,
				7, 0, 0);

// K_THREAD_DEFINE(CAN2_tid, 2048,
// 				CAN2_thread, 
// 				NULL, NULL, NULL, 
// 				5, 0, 0);

K_THREAD_DEFINE(CAN_loopback_tid, 2048,
				CAN_loopback_thread, 
				NULL, NULL, NULL, 
				5, 0, 0);

K_THREAD_DEFINE(USB_CAN_Err_tid, 1024,
				USB_CAN_ERR_thread, 
				NULL, NULL, NULL, 
				2, 0, 0);


int main_err = 0;

int main(void)
{
	#ifdef CONFIG_USB_DEVICE_STACK_NEXT
	const struct device *gs_usb_dev = DEVICE_DT_GET(DT_NODELABEL(gs_usb0));
	const struct device *can_local = DEVICE_DT_GET(DT_NODELABEL(can_loopback0));
	const struct device *can_2 = DEVICE_DT_GET(DT_NODELABEL(fdcan2));
	static struct gs_usb_ops ops;
    ops.event = gs_usb_event_handler;


	// const struct device *channels[] =
	// {
	// 	DEVICE_DT_GET(DT_NODELABEL(can_loopback0)),
    //     DEVICE_DT_GET(DT_NODELABEL(fdcan2)),
    // };
    
	main_err = can_channels_validate(can_channels);
	if (main_err != 0) 
	{
		LOG_ERR("can_channels_validate failed (err %d)", main_err);
		return main_err;
	}

	main_err = gs_usb_register(gs_usb_dev, can_channels, ARRAY_SIZE(can_channels), &ops, NULL);
	if (main_err != 0) 
	{
		LOG_ERR("failed to register gs_usb (err %d)", main_err);
		return main_err;
	}

	if (!device_is_ready(gs_usb_dev)) 
	{
		LOG_ERR("gs_usb not ready");
		return -ENODEV;
	}

	/* New USB stack initialization */
	main_err = usbd_add_descriptor(&usbd, &lang);
	if (main_err != 0) 
	{
		LOG_ERR("failed to add language descriptor (err %d)", main_err);
		return main_err;
	}

	main_err = usbd_add_descriptor(&usbd, &mfr);
	if (main_err != 0) 
	{
		LOG_ERR("failed to add manufacturer descriptor (err %d)", main_err);
		return main_err;
	}

	main_err = usbd_add_descriptor(&usbd, &product);
	if (main_err != 0) 
	{
		LOG_ERR("failed to add product descriptor (err %d)", main_err);
		return main_err;
	}

	main_err = usbd_add_descriptor(&usbd, &sn);
	if (main_err != 0) 
	{
		LOG_ERR("failed to add serial number descriptor (err %d)", main_err);
		return main_err;
	}

	main_err = usbd_add_configuration(&usbd, USBD_SPEED_FS, &fs_config);
	if (main_err != 0) 
	{
		LOG_ERR("failed to add full-speed configuration (err %d)", main_err);
		return main_err;
	}

	main_err = usbd_register_class(&usbd, "gs_usb_0", USBD_SPEED_FS, 1);
	if (main_err != 0) 
	{
		LOG_ERR("failed to register gs_usb class (err %d)", main_err);
		return main_err;
	}

	main_err = usbd_device_set_code_triple(&usbd, USBD_SPEED_FS, 0, 0, 0);
	if (main_err != 0) 
	{
		LOG_ERR("failed to set code triple (err %d)", main_err);
		return main_err;
	}

	/* Set USB version to 2.0.1 to trigger BOS descriptor read (required for WinUSB) */
	main_err = usbd_device_set_bcd_usb(&usbd, USBD_SPEED_FS, USB_SRN_2_0_1);
	if (main_err != 0) 
	{
		LOG_ERR("failed to set FS bcdUSB (err %d)", main_err);
		return main_err;
	}

	/* Set device version */
	main_err = usbd_device_set_bcd_device(&usbd, APP_VERSION_BCD);
	if (main_err != 0) 
	{
		LOG_ERR("failed to set bcdDevice (err %d)", main_err);
		return main_err;
	}

	main_err = usbd_add_descriptor(&usbd, &bos_lpm);
	if (main_err != 0) 
	{
		LOG_ERR("failed to add BOS LPM descriptor (err %d)", main_err);
		return main_err;
	}

	main_err = usbd_init(&usbd);
	if (main_err != 0) 
	{
		LOG_ERR("failed to initialize USB device (err %d)", main_err);
		return main_err;
	}

	main_err = usbd_enable(&usbd);
	if (main_err != 0) 
	{
		LOG_ERR("failed to enable USB device (err %d)", main_err);
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
