#include <K_RTT_Thread.h>
#include <zephyr/kernel.h>
#include <zephyr/debug/thread_analyzer.h>   // thread_analyzer_run / struct thread_analyzer_info
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/usbd_msg.h>
#include <USB.h>

LOG_MODULE_REGISTER(rtt_thread, LOG_LEVEL_INF);

int LOG_INFO = 0;
int err = 0;

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

void RTT_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    uint32_t n = 0;

    while (1) 
	{
		report_stack_usage();
        k_sleep(K_MSEC(5000));
    }
}