#include "fake_rtthread_log.h"
#include "logging/imu_log_service.h"

#include <assert.h>

static imu_log_event_t event(void)
{
    return (imu_log_event_t){
        .kind = IMU_LOG_CALIBRATION_COMPLETE,
    };
}

int main(void)
{
    imu_log_event_t received;

    fake_rtthread_log_reset();
    assert(imu_log_service_init());

    assert(imu_log_submit((imu_log_event_t){
        .kind = IMU_LOG_INITIAL_STATE,
    }));
    assert(imu_log_submit((imu_log_event_t){
        .kind = IMU_LOG_CALIBRATION_COMPLETE,
    }));
    assert(fake_rtthread_log_receive(&received, sizeof received));
    assert(received.kind == IMU_LOG_INITIAL_STATE);
    assert(fake_rtthread_log_receive(&received, sizeof received));
    assert(received.kind == IMU_LOG_CALIBRATION_COMPLETE);

    for (unsigned i = 0u; i < 8u; ++i) {
        assert(imu_log_submit(event()));
    }
    assert(imu_log_drop_count() == 0u);
    assert(!imu_log_submit(event()));
    assert(imu_log_drop_count() == 1u);
    return 0;
}
