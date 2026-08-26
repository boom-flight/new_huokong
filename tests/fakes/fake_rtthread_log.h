#ifndef TEST_FAKE_RTTHREAD_LOG_H
#define TEST_FAKE_RTTHREAD_LOG_H

#include "rtthread.h"

void fake_rtthread_log_reset(void);
void fake_rtthread_log_set_thread_init_result(rt_err_t result);
void fake_rtthread_log_set_thread_startup_result(rt_err_t result);
void fake_rtthread_log_set_queue_init_result(rt_err_t result);
void fake_rtthread_log_set_run_worker_on_delay(int enabled);
int fake_rtthread_log_receive(void *buffer, rt_size_t size);
int fake_rtthread_log_queue_active(void);
unsigned fake_rtthread_log_queue_detach_count(void);
unsigned fake_rtthread_log_defunct_execute_count(void);
int fake_rtthread_log_deferred_cleanup_pending(void);

#endif
