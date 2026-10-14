#include <K_CAN_Thread.h>
#include <zephyr/logging/log.h>
#include <M_S_Control.h>
#include "Basic_CAN.h"
LOG_MODULE_REGISTER(can_thread, LOG_LEVEL_INF);

void CAN_loopback_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    const struct device *can = DEVICE_DT_GET(DT_NODELABEL(can_loopback0));
    enum can_state state;
    struct can_frame CAN_Communicate_Frame =
    {
        .id = 0x5FF,
        .dlc = 8,
        .data = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88},
    };

    // LOG_INF("can_loopback thread start");

    while (1) 
    {
        can_send(can, &CAN_Communicate_Frame, K_MSEC(100), NULL, NULL);
        CAN_Communicate_Frame.data[0]++;
        k_sleep(K_MSEC(500));
    }
}

void CAN2_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) 
	{
        k_sleep(K_MSEC(1));
    }
}