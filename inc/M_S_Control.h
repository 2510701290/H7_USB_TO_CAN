#ifndef M_S_CONTROL_H_
#define M_S_CONTROL_H_

#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <zephyr/devicetree.h> 



class _M_S_Control
{
    private:
        struct k_sem sem;
        bool S_State;
        bool *E_CAN_State;
        const uint16_t *can_mod;
        const can_rx_callback_t *CAN_RX_Cplt;
        const struct device **can_dev;
        const struct can_filter *const *Fil;
        const uint32_t *bitrate;
    public:
        _M_S_Control(bool *E_CAN_State,
                     const uint16_t *can_mod,
                     const can_rx_callback_t *CAN_RX_Cplt,
                     const struct device **can_dev,
                     const struct can_filter *const *Fil,
                     const uint32_t *bitrate)
                    :S_State(false), E_CAN_State(E_CAN_State), can_mod(can_mod), CAN_RX_Cplt(CAN_RX_Cplt),can_dev(can_dev), Fil(Fil), bitrate(bitrate)
                    {k_sem_init(&sem, 1, 1);}
        
        /* delete cpoy function */
        _M_S_Control(const _M_S_Control&) = delete;
        _M_S_Control& operator=(const _M_S_Control&) = delete;

        int C_M_TO_S(struct usbd_context *const uds_ctx);

        int C_S_TO_M(  struct usbd_context *const uds_ctx,
                        const struct device *usb_dev,
                        struct gs_usb_ops *res_ops);
        
        bool Get_S_State(k_timeout_t timeout);
        bool Get_E_CAN_State(size_t channel, k_timeout_t timeout);
};
extern _M_S_Control M_S_Control;
#endif