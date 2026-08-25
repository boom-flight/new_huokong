#ifndef IMU_SERVICE_H
#define IMU_SERVICE_H

#include "imu/imu_snapshot.h"

#include <stdbool.h>

bool imu_service_init(void);
bool imu_snapshot_read(imu_snapshot_t *out);
void imu_service_record_telemetry_drop(void);

#endif
