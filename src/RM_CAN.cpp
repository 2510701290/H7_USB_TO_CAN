#include "RM_CAN.h"
#include "M_S_Control.h"

const struct can_filter RM_CAN2_Fil = { .id = 0, .mask = 0, .flags = 0 };
const struct can_filter RM_CAN3_Fil = { .id = 0, .mask = 0, .flags = 0 };

K_MSGQ_DEFINE(rm_can2_q, 8,8,2);
K_MSGQ_DEFINE(rm_can3_q, 8,8,2);

__attribute__((weak)) void CAN2_RX_Cplt(const struct device *dev, 
                                        struct can_frame *frame,
                                        void *user_data)
{
    k_msgq_purge(&rm_can2_q);
    CAN_Get_Frame(frame->data,rm_can2_q);
    
}

__attribute__((weak)) void CAN3_RX_Cplt(const struct device *dev, 
                                        struct can_frame *frame,
                                        void *user_data)
{
    k_msgq_purge(&rm_can3_q);
    CAN_Get_Frame(frame->data,rm_can3_q);
}

void CAN_RM_Start(  const struct device *can,
                    uint16_t can_mod,
                    can_rx_callback_t CAN_RX_Cplt,
                    const struct can_filter *filter)
{
    int rec = 0;
    if(can != NULL)
    {
        can_stop(can);
        can_set_mode(can, can_mod);
        can_set_bitrate(can, 1000000);
        if(can_mod == CAN_MODE_FD)
            can_set_bitrate_data(can, 5000000);
        CAN_Clear_Filters(can);
        can_add_rx_filter(can, CAN_RX_Cplt, NULL, filter);
        can_start(can);
    }
}

void CAN_RM_Stop(const struct device *can)
{
    int rec = 0;
    if(can != NULL)
    {
        can_stop(can);
        CAN_Clear_Filters(can);
    }
}

void CAN_Clear_Filters(const struct device *can)
{
    int n_std = can_get_max_filters(can, false);
    int n_ext = can_get_max_filters(can, true);
    for (int id = 0; id < n_std + n_ext; id++) 
        can_remove_rx_filter(can, id);
}

void Send_RM(const struct device *can, uint16_t Id, int16_t M_201, int16_t M_202, int16_t M_203, int16_t M_204)
{
    struct can_frame Frame =
    {
        .id = Id,
        .dlc = 8,
    };
	Frame.data[0] = (uint8_t)(M_201 >> 8);
	Frame.data[1] = (uint8_t)M_201;
	Frame.data[2] = (uint8_t)(M_202 >> 8);
	Frame.data[3] = (uint8_t)M_202;
	Frame.data[4] = (uint8_t)(M_203 >> 8);
	Frame.data[5] = (uint8_t)M_203;
	Frame.data[6] = (uint8_t)(M_204 >> 8);
	Frame.data[7] = (uint8_t)M_204;
	can_send(can, &Frame, K_NO_WAIT, NULL, NULL);
}