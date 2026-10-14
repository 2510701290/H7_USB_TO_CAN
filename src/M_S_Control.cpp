/*注意！不可以在中断使用M_S_Control内的函数*/
#include "Basic_USB.h"
#include "Basic_CAN.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>
#include "M_S_Control.h"

LOG_MODULE_REGISTER(M_S_Control, LOG_LEVEL_INF);

static bool E_CAN_State_tab[CONFIG_USBD_GS_USB_MAX_CHANNELS] = { false, false };
static const uint16_t can_mod_tab[CONFIG_USBD_GS_USB_MAX_CHANNELS] = { CAN_MODE_LOOPBACK, CAN_MODE_NORMAL };
static const can_rx_callback_t cplt_tab[CONFIG_USBD_GS_USB_MAX_CHANNELS] = { CAN_LB_RX_Cplt, CAN2_RX_Cplt };
static const struct can_filter *const fil_tab[CONFIG_USBD_GS_USB_MAX_CHANNELS] = { &BASIC_CANLB_Fil, &BASIC_CAN2_Fil };
static const uint32_t br_tab[CONFIG_USBD_GS_USB_MAX_CHANNELS] = { 1000000, 1000000 };

/* M_S_Control类的实例化，
   传入参数 ：             CAN通道状态         CAN模式   CAN接收回调函数  CAN设备   CAN过滤器  CAN波特率 */
_M_S_Control M_S_Control(E_CAN_State_tab, can_mod_tab, cplt_tab, can_channels, fil_tab, br_tab);

/*+ return the number of successfully started CAN channels
  -  0: all channels started successfully
  - -1: usbd_disable false */
int _M_S_Control::C_M_TO_S(struct usbd_context *const uds_ctx)
{
    int err = 0,rec = 0;
    LOG_ERR("delecting USBTOCAN");
    if(k_sem_take(&this->sem, K_FOREVER) == 0)
    {
        err = usbd_disable(uds_ctx);
        if(err != 0)
        {
            LOG_ERR("usbd_disable err = %d",err);
            k_sem_give(&this->sem);
            return -1;
        }
        this->S_State = true;
        for(size_t i = 0; i < CONFIG_USBD_GS_USB_MAX_CHANNELS; i++)
        {
            err = Basic_CAN_Start(  this->can_dev[i],
                                    this->can_mod[i],
                                    this->CAN_RX_Cplt[i],
                                    this->Fil[i],
                                    this->bitrate[i]);
            if(err != 0)
            {
                LOG_ERR("Basic_Std_CAN_Start channel %d err = %d", i, err);
                this->E_CAN_State[i] = false;
            }
            else
            {
                LOG_INF("Basic_Std_CAN_Start channel %d ok", i);
                this->E_CAN_State[i] = true;
            }
            this->S_State &= this->E_CAN_State[i];
            rec |= (this->E_CAN_State[i] ? 1 : 0) << i;
        }
    }
    k_sem_give(&this->sem);
    return rec;
}


/*+ return the number of successfully stopped CAN channels  
  -  0: all channels stopped successfully
  - -1: gs_usb_register false - -2:usbd_enable false */
int _M_S_Control::C_S_TO_M( struct usbd_context *const uds_ctx,
                            const struct device *usb_dev,
                            struct gs_usb_ops *res_ops)
{
    int err = 0,rec = 0;
    if(k_sem_take(&this->sem, K_FOREVER) == 0)
    {
        LOG_ERR("Enabling USBTOCAN");
        for(size_t i = 0; i < CONFIG_USBD_GS_USB_MAX_CHANNELS; i++)
        {
            err = Basic_CAN_Stop(this->can_dev[i]);
            if(err == 0)
            {
                LOG_ERR("Basic_Std_CAN_Stop channel %d close ok", i);
                this->E_CAN_State[i] = false;
            }
            else
            {
                LOG_ERR("Basic_Std_CAN_Stop channel %d close err = %d", i, err);
            }
            rec |= (this->E_CAN_State[i] ? 0 : 1) << i;
        }
        err = gs_usb_register(usb_dev, this->can_dev, CONFIG_USBD_GS_USB_MAX_CHANNELS, res_ops, NULL);
        if(err != 0)
        {
            LOG_ERR("gs_usb_register err = %d",err);
            k_sem_give(&this->sem);
            return -1;
        }
        else
            LOG_INF("gs_usb_register ok");
        err = usbd_enable(uds_ctx);
        if(err != 0 && err != -ENOMSG)
        {
            LOG_ERR("usbd_enable err = %d",err);
            k_sem_give(&this->sem);
            return -2;
        }
        else
            LOG_INF("usbd_enable ok");
        this->S_State = false;
    }
    k_sem_give(&this->sem); 
    return rec;
}

bool _M_S_Control::Get_S_State(k_timeout_t timeout)
{
    if(k_sem_take(&this->sem, timeout) == 0)
    {
        bool r_bool = this->S_State;
        k_sem_give(&this->sem); 
        return r_bool;
    }
    else
    {
        k_sem_give(&this->sem); 
        return false;
    }
        
}

bool _M_S_Control::Get_E_CAN_State(size_t channel, k_timeout_t timeout)
{
    if(k_sem_take(&this->sem, timeout) == 0)
    {
        bool r_bool = this->E_CAN_State[channel];
        k_sem_give(&this->sem);
        return r_bool;
    }
    else
    {
        k_sem_give(&this->sem);
        return false;
    }
}