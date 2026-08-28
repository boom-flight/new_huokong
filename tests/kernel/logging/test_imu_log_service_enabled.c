#include "logging/imu_log_service.h"

#include <assert.h>

int main(void)
{
    imu_log_event_t event = {
        .kind = IMU_LOG_INITIAL_STATE,
    };

    assert(imu_log_service_init());
    assert(imu_log_submit(event));
    assert(imu_log_drop_count() == 0u);
    assert(imu_log_service_deinit());
    return 0;
}
