#ifndef IMU_TYPES_H
#define IMU_TYPES_H

typedef struct {
    float x;
    float y;
    float z;
} imu_vec3f_t;

typedef struct {
    float w;
    float x;
    float y;
    float z;
} imu_quatf_t;

#endif
