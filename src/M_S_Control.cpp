#include "USB.h"
#include "RM_CAN.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>
#include "M_S_Control.h"

LOG_MODULE_REGISTER(M_S_Control, LOG_LEVEL_INF);

bool M_State = true;
/*注意！不可以在中断使用M_S_Control内的函数*/


void C_M_TO_S(struct usbd_context *const uds_ctx,
              uint16_t can_mod,
              can_rx_callback_t CAN_RX_Cplt,
              const struct device *can,
              const struct can_filter Fil)
{
    int err = 0;
    LOG_ERR("delecting USBTOCAN");
    err = usbd_disable(uds_ctx);
    CAN_RM_Start(can,
                 can_mod,
                 CAN_RX_Cplt,
                 &Fil);
    LOG_ERR("delect USBTOCAN err = %d",err);
}

void C_S_TO_M(struct usbd_context *const uds_ctx,
              const struct device *usb_dev,
              const struct device **can_dev,
              size_t nchannels,
              struct gs_usb_ops *res_ops,
              const struct device *can)
{
    int err = 0;
    CAN_RM_Stop(can);
    err = gs_usb_register(usb_dev, can_dev, nchannels, res_ops, NULL);
    LOG_ERR("gs_usb_register err = %d",err);
    err = usbd_enable(uds_ctx);
    LOG_ERR("usbd_enable err = %d",err);
}
