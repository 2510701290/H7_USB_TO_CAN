#ifndef K_CAN_THREAD_H_
#define K_CAN_THREAD_H_

#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <zephyr/devicetree.h> 

void CAN2_thread(void *p1, void *p2, void *p3);
void CAN_loopback_thread(void *p1, void *p2, void *p3);
#endif