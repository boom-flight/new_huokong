#ifndef MAHONY_H
#define MAHONY_H

#include <stdbool.h>

#include "include/imu_types.h"

typedef struct {
    imu_quatf_t q;
    imu_vec3f_t integral_fb;
    float two_kp;
    float two_ki;
    bool valid;
} mahony_t;

void mahony_init_from_gravity(mahony_t *self,
                              imu_vec3f_t accel_g,
                              float kp,
                              float ki);
void mahony_invalidate(mahony_t *self);
bool mahony_update(mahony_t *self,
                   imu_vec3f_t gyro_rad_s,
                   imu_vec3f_t accel_g,
                   bool correction_valid,
                   float dt_s);
imu_quatf_t mahony_quaternion(const mahony_t *self);
imu_vec3f_t mahony_euler_deg(const mahony_t *self);

#endif
