#ifndef RTT_THREAD_H_
#define RTT_THREAD_H_

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>

extern int LOG_INFO;
extern int err;

void RTT_thread(void *p1, void *p2, void *p3);

#endif