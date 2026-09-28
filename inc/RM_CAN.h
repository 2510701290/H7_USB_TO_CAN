#ifndef RM_CAN_H_
#define RM_CAN_H_

#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <zephyr/devicetree.h> 

#define CAN_Get_Frame(data_ptr, q_name)   (k_msgq_put(&(q_name), (data_ptr), K_NO_WAIT))

extern const struct can_filter RM_CAN2_Fil;
extern const struct can_filter RM_CAN3_Fil;

extern struct k_msgq rm_can2_q;

void CAN2_RX_Cplt(const struct device *dev, struct can_frame *frame, void *user_data);
void CAN3_RX_Cplt(const struct device *dev, struct can_frame *frame, void *user_data);
void CAN_Clear_Filters(const struct device *can);

void CAN_RM_Start(  const struct device *can,
                    uint16_t can_mod,
                    can_rx_callback_t CAN_RX_Cplt,
                    const struct can_filter *filter);

void CAN_RM_Stop(const struct device *can);

#endif