#ifndef M_S_CONTROL_H_
#define M_S_CONTROL_H_

void C_M_TO_S(struct usbd_context *const uds_ctx,
              uint16_t can_mod,
              can_rx_callback_t CAN_RX_Cplt,
              const struct device *can,
              const struct can_filter Fil);

void C_S_TO_M(struct usbd_context *const uds_ctx,
              const struct device *usb_dev,
              const struct device **can_dev,
              size_t nchannels,
              struct gs_usb_ops *res_ops,
              const struct device *can);
extern bool M_State;
#endif