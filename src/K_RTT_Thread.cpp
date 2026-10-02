#include <K_RTT_Thread.h>
#include <zephyr/kernel.h>
#include <zephyr/debug/thread_analyzer.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/usbd_msg.h>
#include <USB.h>
#include "RM_CAN.h"
#include "M_S_Control.h"

LOG_MODULE_REGISTER(rtt_thread, LOG_LEVEL_INF);

int RTT_LOG_INFO = 0;
char RTT_RX_BUF[32];

/* 每个线程调用一次；当前上下文就是 RTT_thread，所以 LOG_INF 由本模块发出 */
static void stack_info_cb(struct thread_analyzer_info *info)
{
    unsigned int pcnt = (unsigned int)((info->stack_used * 100U) / info->stack_size);

#ifdef CONFIG_THREAD_RUNTIME_STATS
    LOG_INF("%-14s stack %4zu/%4zu used %3u%% free %4zu cpu %3u%%",
            info->name, info->stack_used, info->stack_size, pcnt,
            info->stack_size - info->stack_used, (unsigned int)info->utilization);
#else
    LOG_INF("%-14s stack %4zu/%4zu used %3u%% free %4zu",
            info->name, info->stack_used, info->stack_size, pcnt,
            info->stack_size - info->stack_used);
#endif
}

static void report_stack_usage(void)//检查线程占用使用，正常不要开
{
    LOG_INF("---- thread stack usage ----");
    thread_analyzer_run(stack_info_cb, 0);   /* 0 = CPU id（单核固定写 0） */
    LOG_INF("----------------------------");
}

void RTT_TX_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    uint32_t n = 0;
    uint8_t data[64];
    while (1) 
	{
		// report_stack_usage();
        if (k_msgq_get(&rm_can2_q, data, K_FOREVER) == 0)
        {
            LOG_INF("%X %X %X %X %X %X %X %X",
                    data[0], data[1], data[2], data[3],
                    data[4], data[5], data[6], data[7]);
            Send_RM(DEVICE_DT_GET(DT_NODELABEL(fdcan2)), 0x010, 0, 0, 0, 0);
        }
        
        // LOG_INF("RTT_TX_thread running %d", n++);
        // k_sleep(K_MSEC(10));
    }
}

void RTT_RX_thread(void *p1, void *p2, void *p3)
{
    int err = 0;
    
    while(1)
    {
        if(SEGGER_RTT_HasData(0)) 
        {
            SEGGER_RTT_Read(0, RTT_RX_BUF, sizeof(RTT_RX_BUF));
            switch(RTT_RX_BUF[0])
            {
                case 'c':
                    C_M_TO_S(usb_main_get(),
                             CAN_MODE_NORMAL,
                             CAN2_RX_Cplt,
                             DEVICE_DT_GET( DT_NODELABEL(fdcan2)),
                             RM_CAN2_Fil);
                    M_State = false;
                    break;

                /*注意！下位机虽然重新开启了端点，但上位机需要寻找USB才能重新通信*/
                case 'a':
                    C_S_TO_M(usb_main_get(),
                             gs_usb_dev,can_channels,
                             ARRAY_SIZE(can_channels),
                             &ops,
                             DEVICE_DT_GET( DT_NODELABEL(fdcan2)));
                    M_State = false;
                    break;
                default:break;
            }

        }
        k_sleep(K_MSEC(10));
    }
}