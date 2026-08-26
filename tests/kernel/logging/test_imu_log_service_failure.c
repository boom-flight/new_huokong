#include "fake_rtthread_log.h"
#include "logging/imu_log_service.h"

#include <assert.h>

int main(void)
{
    struct rt_thread fake_thread = {0};

    fake_rtthread_log_reset();
    assert(rt_thread_init(&fake_thread, "fake", NULL, NULL, NULL, 0u,
                          0u, 0u) == RT_EOK);
    assert(fake_thread.state == FAKE_RT_THREAD_INIT);
    assert(rt_thread_startup(&fake_thread) == RT_EOK);
    assert(fake_thread.state == FAKE_RT_THREAD_RUNNING);
    assert(rt_thread_suspend(&fake_thread) == RT_EOK);
    assert(fake_thread.state == FAKE_RT_THREAD_SUSPENDED);
    assert(rt_thread_detach(&fake_thread) == RT_EOK);
    assert(fake_thread.state == FAKE_RT_THREAD_DEFUNCT);
    assert(fake_rtthread_log_deferred_cleanup_pending());
    assert(rt_thread_init(&fake_thread, "fake", NULL, NULL, NULL, 0u,
                          0u, 0u) != RT_EOK);
    rt_defunct_execute();
    assert(!fake_rtthread_log_deferred_cleanup_pending());
    assert(fake_thread.state == FAKE_RT_THREAD_DETACHED);
    assert(rt_thread_init(&fake_thread, "fake", NULL, NULL, NULL, 0u,
                          0u, 0u) == RT_EOK);
    fake_rtthread_log_reset();

    fake_rtthread_log_set_queue_init_result(-1);

    assert(!imu_log_service_init());
    assert(!imu_log_submit((imu_log_event_t){
        .kind = IMU_LOG_CALIBRATION_COMPLETE,
    }));
    assert(imu_log_drop_count() == 1u);
    imu_log_service_deinit();
    imu_log_service_deinit();

    fake_rtthread_log_set_queue_init_result(RT_EOK);
    assert(imu_log_service_init());
    assert(imu_log_submit((imu_log_event_t){
        .kind = IMU_LOG_CALIBRATION_COMPLETE,
    }));
    assert(fake_rtthread_log_receive(&(imu_log_event_t){0},
                                     sizeof(imu_log_event_t)));

    fake_rtthread_log_set_run_worker_on_delay(1);
    assert(imu_log_service_deinit());
    imu_log_service_deinit();
    assert(imu_log_service_init());
    assert(imu_log_service_deinit());
    assert(fake_rtthread_log_defunct_execute_count() == 2u);
    assert(!fake_rtthread_log_deferred_cleanup_pending());

    assert(imu_log_service_init());
    fake_rtthread_log_set_run_worker_on_delay(0);
    assert(!imu_log_service_deinit());
    assert(fake_rtthread_log_queue_active());
    return 0;
}
