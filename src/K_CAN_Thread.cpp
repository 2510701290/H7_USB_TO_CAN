#include <K_CAN_Thread.h>
#include <zephyr/logging/log.h>
#include <M_S_Control.h>
#include "Basic_CAN.h"
LOG_MODULE_REGISTER(can_thread, LOG_LEVEL_INF);

extern uint32_t CAN_TX_Num;

static inline int64_t now_us(void)
{
    return (int64_t)k_ticks_to_us_floor64((uint64_t)k_uptime_ticks());
}

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
    struct can_frame CAN_DM_Clear_Err_Frame =
    {
        .id = 0x01,
        .dlc = can_bytes_to_dlc(8),
        .flags = CAN_FRAME_FDF | CAN_FRAME_BRS,
        .data = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB},
    };
    struct can_frame CAN_DM_Start_Frame =
    {
        .id = 0x01,
        .dlc = can_bytes_to_dlc(8),
        .flags = CAN_FRAME_FDF | CAN_FRAME_BRS,
        .data = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC},
    };
    struct can_frame CAN_DM_Zero_Frame =
    {
        .id = 0x01,
        .dlc = can_bytes_to_dlc(8),
        .flags = CAN_FRAME_FDF | CAN_FRAME_BRS,
        .data = {0x7F, 0xFF, 0x7F, 0xF0, 0x00, 0x00, 0x07, 0xFF},
    };
    struct can_frame CAN_DM_F_Frame =
    {
        .id = 0x03,
        .dlc = can_bytes_to_dlc(8),
        .flags = CAN_FRAME_FDF | CAN_FRAME_BRS,
        .data = {0x7f, 0xff, 0x7f, 0xf0, 0x00, 0x00, 0x06, 0x48},
    };
    const struct device *can = DEVICE_DT_GET(DT_NODELABEL(fdcan2));
    for(int i = 0; i < 6; i++)
    {
        can_send(can, &CAN_DM_Clear_Err_Frame, K_MSEC(1), NULL, NULL);
        k_sleep(K_USEC(300));
        can_send(can, &CAN_DM_Start_Frame, K_MSEC(1), NULL, NULL);
        k_sleep(K_USEC(300));
        CAN_DM_Start_Frame.id++;
        CAN_DM_Clear_Err_Frame.id++;
    }
    CAN_DM_Clear_Err_Frame.id = 0x09;
    CAN_DM_Start_Frame.id = 0x09;
    can_send(can, &CAN_DM_Clear_Err_Frame, K_MSEC(1), NULL, NULL);
    k_sleep(K_USEC(300));
    can_send(can, &CAN_DM_Start_Frame, K_MSEC(1), NULL, NULL);
    k_sleep(K_USEC(300));
    
    int64_t next = now_us();
    while (1) {
        if(CAN_DM_Zero_Frame.id!=0x03)
        {
            if(CAN_DM_Zero_Frame.id!=0x06)
                (void)can_send(can, &CAN_DM_Zero_Frame, K_MSEC(1), NULL, NULL);
            else
            {
                CAN_DM_Zero_Frame.id!=0x09;
                (void)can_send(can, &CAN_DM_Zero_Frame, K_MSEC(1), NULL, NULL);
                CAN_DM_Zero_Frame.id = 0x06;
            }
        }
        else
            (void)can_send(can, &CAN_DM_F_Frame, K_MSEC(1), NULL, NULL);
        CAN_TX_Num++;
        CAN_DM_Zero_Frame.id++;
        if (CAN_DM_Zero_Frame.id > 0x06) {
            CAN_DM_Zero_Frame.id = 0x01;
        }

        next += 166;
        int64_t wait = next - now_us();

        if (wait > 0) {
            k_sleep(K_USEC(wait));
        } else {
            next = now_us();
        }
    }
}