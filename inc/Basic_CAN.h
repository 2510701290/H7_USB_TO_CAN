#ifndef BASIC_CAN_H_
#define BASIC_CAN_H_

#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <zephyr/devicetree.h> 

#define CAN_Get_Frame(data_ptr, q_name)   (k_msgq_put(&(q_name), (data_ptr), K_NO_WAIT))

extern const struct can_filter BASIC_CANLB_Fil;
extern const struct can_filter BASIC_CAN1_Fil;
extern const struct can_filter BASIC_CAN2_Fil;
extern const struct can_filter BASIC_CAN3_Fil;

extern struct k_msgq basic_canlb_q;
extern struct k_msgq basic_can1_q;
extern struct k_msgq basic_can2_q;
extern struct k_msgq basic_can3_q;

void CAN_LB_RX_Cplt(const struct device *dev, struct can_frame *frame, void *user_data);
void CAN1_RX_Cplt(const struct device *dev, struct can_frame *frame, void *user_data);
void CAN2_RX_Cplt(const struct device *dev, struct can_frame *frame, void *user_data);
void CAN3_RX_Cplt(const struct device *dev, struct can_frame *frame, void *user_data);

void CAN_Clear_All_Filters(const struct device *can);
void CAN_Clear_Std_Filters(const struct device *can);

int Basic_CAN_Start(const struct device *can,
                        uint16_t can_mod,
                        can_rx_callback_t CAN_RX_Cplt,
                        const struct can_filter *filter,
                        uint32_t bitrate);

int Basic_CAN_Stop(const struct device *can);
#endif