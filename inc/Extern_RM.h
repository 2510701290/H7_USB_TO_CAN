#ifndef EXTERN_RM_H_
#define EXTERN_RM_H_

#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <zephyr/devicetree.h> 

void Send_RM(const struct device *can, uint16_t Id, int16_t M_201, int16_t M_202, int16_t M_203, int16_t M_204);
#endif