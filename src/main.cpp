#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <USB.h>

#include <K_RTT_Thread.h>
#include <K_CAN_Thread.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

K_THREAD_DEFINE(RTT_tid, 4096,
				RTT_thread, 
				NULL, NULL, NULL,
				3, 0, 0);

// K_THREAD_DEFINE(CAN2_tid, 2048,
// 				CAN2_thread, 
// 				NULL, NULL, NULL, 
// 				5, 0, 0);

K_THREAD_DEFINE(CAN_loopback_tid, 2048,
				CAN_loopback_thread, 
				NULL, NULL, NULL, 
				4, 0, 0);

uint32_t i = 0;

int main(void)
{
	#ifdef CONFIG_USB_DEVICE_STACK_NEXT
	const struct device *gs_usb_dev = DEVICE_DT_GET(DT_NODELABEL(gs_usb0));
	const struct device *can_local = DEVICE_DT_GET(DT_NODELABEL(can_loopback0));
	const struct device *can_2 = DEVICE_DT_GET(DT_NODELABEL(fdcan2));
	struct gs_usb_ops ops = { 0 };

	const struct device *channels[] =
	{
		DEVICE_DT_GET(DT_NODELABEL(can_loopback0)),
        DEVICE_DT_GET(DT_NODELABEL(fdcan2)),
    };
    

	err = gs_usb_register(gs_usb_dev, channels, ARRAY_SIZE(channels), &ops, NULL);
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

	/* New USB stack initialization */
	err = usbd_add_descriptor(&usbd, &lang);
	if (err != 0) 
	{
		LOG_INFO = 1;
		LOG_ERR("failed to add language descriptor (err %d)", err);
		return err;
	}

	err = usbd_add_descriptor(&usbd, &mfr);
	if (err != 0) 
	{
		LOG_INFO = 2;
		LOG_ERR("failed to add manufacturer descriptor (err %d)", err);
		return err;
	}

	err = usbd_add_descriptor(&usbd, &product);
	if (err != 0) 
	{
		LOG_INFO = 3;
		LOG_ERR("failed to add product descriptor (err %d)", err);
		return err;
	}

	err = usbd_add_descriptor(&usbd, &sn);
	if (err != 0) 
	{
		LOG_INFO = 4;
		LOG_ERR("failed to add serial number descriptor (err %d)", err);
		return err;
	}

	err = usbd_add_configuration(&usbd, USBD_SPEED_FS, &fs_config);
	if (err != 0) 
	{
		LOG_INFO = 5;
		LOG_ERR("failed to add full-speed configuration (err %d)", err);
		return err;
	}

	err = usbd_register_class(&usbd, "gs_usb_0", USBD_SPEED_FS, 1);
	if (err != 0) 
	{
		LOG_INFO = 6;
		LOG_ERR("failed to register gs_usb class (err %d)", err);
		return err;
	}

	err = usbd_device_set_code_triple(&usbd, USBD_SPEED_FS, 0, 0, 0);
	if (err != 0) 
	{
		LOG_INFO = 7;
		LOG_ERR("failed to set code triple (err %d)", err);
		return err;
	}

	/* Set USB version to 2.0.1 to trigger BOS descriptor read (required for WinUSB) */
	err = usbd_device_set_bcd_usb(&usbd, USBD_SPEED_FS, USB_SRN_2_0_1);
	if (err != 0) 
	{
		LOG_INFO = 8;
		LOG_ERR("failed to set FS bcdUSB (err %d)", err);
		return err;
	}

	/* Set device version */
	err = usbd_device_set_bcd_device(&usbd, APP_VERSION_BCD);
	if (err != 0) 
	{
		LOG_INFO = 9;
		LOG_ERR("failed to set bcdDevice (err %d)", err);
		return err;
	}

	err = usbd_add_descriptor(&usbd, &bos_lpm);
	if (err != 0) 
	{
		LOG_INFO = 10;
		LOG_ERR("failed to add BOS LPM descriptor (err %d)", err);
		return err;
	}

	err = usbd_add_descriptor(&usbd, &bos_msosv2);
	if (err != 0) 
	{
		LOG_INFO = 11;
		LOG_ERR("failed to add BOS MSOS2 descriptor (err %d)", err);
		return err;
	}

	err = usbd_init(&usbd);
	if (err != 0) 
	{
		LOG_INFO = 12;
		LOG_ERR("failed to initialize USB device (err %d)", err);
		return err;
	}

	err = usbd_enable(&usbd);
	if (err != 0) 
	{
		LOG_INFO = 13;
		LOG_ERR("failed to enable USB device (err %d)", err);
		return err;
	}
	#else
		/* Old USB stack */
		err = usb_enable(NULL);
		if (err != 0) 
		{
			LOG_INFO = 99;
			LOG_ERR("failed to enable USB (err %d)", err);
			return err;
		}
	#endif


	return 0;
}
