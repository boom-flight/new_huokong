#include "fake_rtthread_log.h"
#include "logging/imu_log_service.h"

#include <assert.h>

int main(void)
{
    fake_rtthread_log_reset();
    fake_rtthread_log_set_thread_init_result(-1);
    assert(!imu_log_service_init());
    assert(!fake_rtthread_log_queue_active());
    assert(fake_rtthread_log_queue_detach_count() == 1u);
    assert(!fake_rtthread_log_deferred_cleanup_pending());
    imu_log_service_deinit();
    imu_log_service_deinit();

    fake_rtthread_log_set_thread_init_result(RT_EOK);
    fake_rtthread_log_set_thread_startup_result(-1);

    assert(!imu_log_service_init());
    assert(!fake_rtthread_log_queue_active());
    assert(fake_rtthread_log_queue_detach_count() == 2u);
    assert(!fake_rtthread_log_deferred_cleanup_pending());
    imu_log_service_deinit();
    imu_log_service_deinit();

    fake_rtthread_log_set_thread_startup_result(RT_EOK);
    assert(imu_log_service_init());
    assert(imu_log_submit((imu_log_event_t){
        .kind = IMU_LOG_CALIBRATION_COMPLETE,
    }));
    assert(imu_log_drop_count() == 0u);
    return 0;
}
