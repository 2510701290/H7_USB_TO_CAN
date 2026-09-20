#include <K_CAN_Thread.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(can_thread, LOG_LEVEL_INF);

void CAN2_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    const struct device *can = DEVICE_DT_GET(DT_NODELABEL(fdcan2));
    enum can_state state;
    struct can_frame T_Frame =
    {
        .id = 0x5FF,
        .dlc = 8,
        .data = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88},
    };
    int ret;

    while (1) 
	{
        
        if(can_get_state(can, &state, NULL) == 0 && state != CAN_STATE_STOPPED)
        {
            ret = can_send(can, &T_Frame, K_MSEC(100), NULL, NULL);
            if (ret != 0) 
                LOG_ERR("can_send err %d", ret);
        }
        else
            LOG_INF("state: %d", state);
        T_Frame.data[0]++;
        k_sleep(K_MSEC(500));
    }
}

void CAN_loopback_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    const struct device *can = DEVICE_DT_GET(DT_NODELABEL(can_loopback0));
    enum can_state state;
    struct can_frame T_Frame =
    {
        .id = 0x5FF,
        .dlc = 8,
        .data = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88},
    };
    int ret;

    while (1) 
    {
        ret = can_send(can, &T_Frame, K_MSEC(100), NULL, NULL);
        if (ret != 0) 
            LOG_ERR("can_send err %d", ret);
        T_Frame.data[0]++;
        k_sleep(K_MSEC(500));
    }
}