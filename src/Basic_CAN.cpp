#include "Basic_CAN.h"
#include "M_S_Control.h"

const struct can_filter BASIC_CANLB_Fil = { .id = 0, .mask = 0, .flags = 0 };
const struct can_filter BASIC_CAN1_Fil = { .id = 0, .mask = 0, .flags = 0 };
const struct can_filter BASIC_CAN2_Fil = { .id = 0, .mask = 0, .flags = 0 };
const struct can_filter BASIC_CAN3_Fil = { .id = 0, .mask = 0, .flags = 0 };

K_MSGQ_DEFINE(basic_canlb_q, 8,8,2);
K_MSGQ_DEFINE(basic_can1_q, 8,8,2);
K_MSGQ_DEFINE(basic_can2_q, 8,8,2);
K_MSGQ_DEFINE(basic_can3_q, 8,8,2);

__attribute__((weak)) void CAN_LB_RX_Cplt(const struct device *dev, 
                                        struct can_frame *frame,
                                        void *user_data)
{
    k_msgq_purge(&basic_canlb_q);
    CAN_Get_Frame(frame->data,basic_canlb_q);
}

__attribute__((weak)) void CAN1_RX_Cplt(const struct device *dev, 
                                        struct can_frame *frame,
                                        void *user_data)
{
    k_msgq_purge(&basic_can1_q);
    CAN_Get_Frame(frame->data,basic_can1_q);
}

__attribute__((weak)) void CAN2_RX_Cplt(const struct device *dev, 
                                        struct can_frame *frame,
                                        void *user_data)
{
    k_msgq_purge(&basic_can2_q);
    CAN_Get_Frame(frame->data,basic_can2_q);
}

__attribute__((weak)) void CAN3_RX_Cplt(const struct device *dev, 
                                        struct can_frame *frame,
                                        void *user_data)
{
    k_msgq_purge(&basic_can3_q);
    CAN_Get_Frame(frame->data,basic_can3_q);
}


/*-  0: success
  - -1: can is NULL
  - -2: can_stop fail
  - -3: can_add_rx_filter fail
  - -4: can_set_mode fail
  - -5: can_set_bitrate fail
  - -6: can_set_bitrate_data fail
  - -7: can_start fail*/
int Basic_CAN_Start(const struct device *can,
                        uint16_t can_mod,
                        can_rx_callback_t CAN_RX_Cplt,
                        const struct can_filter *filter,
                        uint32_t bitrate)
{
    if(can != NULL)
    {
        int rec = can_stop(can);
        if(rec == 0 || rec == -EALREADY)
            if (can == DEVICE_DT_GET_ONE(zephyr_can_loopback))
                CAN_Clear_Std_Filters(can);
            else
                CAN_Clear_All_Filters(can);
        else
            return -2;

        if(can_add_rx_filter(can, CAN_RX_Cplt, NULL, filter) != 0)
            return -3;

        if(can_set_mode(can, can_mod) != 0)
            return -4;

        if(can_set_bitrate(can, bitrate) != 0)
            return -5;

        if (can_mod & CAN_MODE_FD)
            if(can_set_bitrate_data(can, bitrate) != 0)
                return -6;
        
        if(can_start(can) != 0)
            return -7;

        return 0;
    }
    return -1;
}

/*-  0: success
  - -1: can is NULL
  - -2: can_stop fail */
int Basic_CAN_Stop(const struct device *can)
{
    if(can != NULL)
    {
        int rec = can_stop(can);
        if(rec == 0 || rec == -EALREADY)
        {
            if (can == DEVICE_DT_GET_ONE(zephyr_can_loopback))
                CAN_Clear_Std_Filters(can);
            else
                CAN_Clear_All_Filters(can);
            return 0;
        }
        else
            return -2;
    }
    return -1;
}

void CAN_Clear_All_Filters(const struct device *can)
{
    int n_std = can_get_max_filters(can, false);
    int n_ext = can_get_max_filters(can, true);
    for (int id = 0; id < n_std + n_ext; id++) 
        can_remove_rx_filter(can, id);
}

void CAN_Clear_Std_Filters(const struct device *can)
{
    int n_std = can_get_max_filters(can, false);
    for (int id = 0; id < n_std; id++) 
        can_remove_rx_filter(can, id);
}