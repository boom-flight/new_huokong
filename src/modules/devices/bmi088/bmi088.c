/**
 * @file bmi088.c
 * @brief BMI088 复位、寄存器配置、采样读取和轴变换实现。
 */

#include "bmi088.h"

enum {
    /** @brief 每个总线操作的最大尝试次数。 */
    BMI088_ATTEMPTS = 3,
    /** @brief 加速度计芯片 ID 寄存器地址。 */
    BMI088_ACCEL_CHIP_ID_REG = 0x00,
    /** @brief 预期的加速度计芯片 ID。 */
    BMI088_ACCEL_CHIP_ID = 0x1E,
    /** @brief 加速度计三轴数据起始寄存器地址。 */
    BMI088_ACCEL_DATA_REG = 0x12,
    /** @brief 陀螺仪芯片 ID 寄存器地址。 */
    BMI088_GYRO_CHIP_ID_REG = 0x00,
    /** @brief 预期的陀螺仪芯片 ID。 */
    BMI088_GYRO_CHIP_ID = 0x0F,
    /** @brief 陀螺仪三轴数据起始寄存器地址。 */
    BMI088_GYRO_DATA_REG = 0x02,
};

typedef struct {
    /** @brief 要配置的 BMI088 子器件。 */
    bmi088_target_t target;
    /** @brief 要写入的寄存器地址。 */
    uint8_t reg;
    /** @brief 期望写入并回读的寄存器值。 */
    uint8_t value;
    /** @brief 写入后生效所需的等待时间，单位为毫秒。 */
    uint32_t settle_ms;
} bmi088_register_config_t;

const bmi088_axis_map_t BMI088_AXIS_MAP = {{0u, 1u, 2u}, {1, 1, 1}};

static const bmi088_register_config_t accel_config[] = {
    {BMI088_ACCEL, 0x7Cu, 0x00u, 5u},
    {BMI088_ACCEL, 0x7Du, 0x04u, 5u},
    {BMI088_ACCEL, 0x40u, 0xABu, 0u},
    {BMI088_ACCEL, 0x41u, 0x01u, 0u},
    {BMI088_ACCEL, 0x53u, 0x08u, 0u},
    {BMI088_ACCEL, 0x58u, 0x04u, 0u},
};

static const bmi088_register_config_t gyro_config[] = {
    {BMI088_GYRO, 0x0Fu, 0x00u, 0u},
    {BMI088_GYRO, 0x10u, 0x02u, 0u},
    {BMI088_GYRO, 0x15u, 0x80u, 0u},
    {BMI088_GYRO, 0x16u, 0x00u, 0u},
    {BMI088_GYRO, 0x18u, 0x01u, 0u},
};

/**
 * @brief 以固定次数重试总线读操作。
 * @param bus 总线回调集合。
 * @param target 目标器件。
 * @param reg 起始寄存器地址。
 * @param data 接收数据的缓冲区。
 * @param length 要读取的字节数。
 * @return 任意一次读取成功时返回 true。
 */
static bool read_retry(const bmi088_bus_t *bus, bmi088_target_t target,
                       uint8_t reg, uint8_t *data, size_t length)
{
    for (unsigned attempt = 0u; attempt < BMI088_ATTEMPTS; ++attempt) {
        if (bus->read(bus->context, target, reg, data, length)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 以固定次数重试单寄存器写操作。
 * @param bus 总线回调集合。
 * @param target 目标器件。
 * @param reg 寄存器地址。
 * @param value 要写入的值。
 * @return 任意一次写入成功时返回 true。
 */
static bool write_retry(const bmi088_bus_t *bus, bmi088_target_t target,
                        uint8_t reg, uint8_t value)
{
    for (unsigned attempt = 0u; attempt < BMI088_ATTEMPTS; ++attempt) {
        if (bus->write(bus->context, target, reg, value)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 写入一个配置寄存器并回读校验。
 * @param bus 总线回调集合。
 * @param config 待应用的寄存器配置。
 * @return 配置写入、总线访问或回读校验结果。
 */
static bmi088_result_t write_and_verify(
    const bmi088_bus_t *bus, const bmi088_register_config_t *config)
{
    bool mismatched = false;

    for (unsigned attempt = 0u; attempt < BMI088_ATTEMPTS; ++attempt) {
        uint8_t readback = 0u;
        if (!bus->write(bus->context, config->target, config->reg,
                        config->value)) {
            continue;
        }
        if (config->settle_ms != 0u) {
            bus->delay_ms(bus->context, config->settle_ms);
        }
        if (!bus->read(bus->context, config->target, config->reg, &readback,
                       1u)) {
            continue;
        }
        if (readback == config->value) {
            return BMI088_OK;
        }
        mismatched = true;
    }
    return mismatched ? BMI088_VERIFY_ERROR : BMI088_BUS_ERROR;
}

/**
 * @brief 按顺序应用一组 BMI088 寄存器配置。
 * @param bus 总线回调集合。
 * @param config 配置项数组。
 * @param count 配置项数量。
 * @return 首个失败配置的结果，全部成功时返回 BMI088_OK。
 */
static bmi088_result_t apply_config(
    const bmi088_bus_t *bus, const bmi088_register_config_t *config,
    size_t count)
{
    for (size_t i = 0u; i < count; ++i) {
        const bmi088_result_t result = write_and_verify(bus, &config[i]);
        if (result != BMI088_OK) {
            return result;
        }
    }
    return BMI088_OK;
}

bool bmi088_axis_map_is_right_handed(const bmi088_axis_map_t *map)
{
    if (map == NULL) {
        return false;
    }

    bool seen[3] = {false, false, false};
    int inversions = 0;
    int sign_product = 1;
    for (size_t i = 0u; i < 3u; ++i) {
        if (map->source_axis[i] >= 3u || seen[map->source_axis[i]] ||
            (map->sign[i] != 1 && map->sign[i] != -1)) {
            return false;
        }
        seen[map->source_axis[i]] = true;
        sign_product *= map->sign[i];
        for (size_t j = 0u; j < i; ++j) {
            if (map->source_axis[j] > map->source_axis[i]) {
                ++inversions;
            }
        }
    }
    const int permutation_sign = (inversions % 2 == 0) ? 1 : -1;
    return permutation_sign * sign_product == 1;
}

bmi088_result_t bmi088_init(bmi088_t *self, bmi088_bus_t bus,
                            bmi088_axis_map_t axis_map, uint8_t *accel_id,
                            uint8_t *gyro_id)
{
    uint8_t dummy = 0u;
    uint8_t found_accel_id = 0u;
    uint8_t found_gyro_id = 0u;

    if (self == NULL || bus.read == NULL || bus.write == NULL ||
        bus.delay_ms == NULL) {
        return BMI088_BAD_ARGUMENT;
    }
    if (!bmi088_axis_map_is_right_handed(&axis_map)) {
        return BMI088_BAD_AXIS_MAP;
    }

    if (!read_retry(&bus, BMI088_ACCEL, BMI088_ACCEL_CHIP_ID_REG, &dummy,
                    1u)) {
        return BMI088_BUS_ERROR;
    }
    bus.delay_ms(bus.context, 1u);

    if (!write_retry(&bus, BMI088_ACCEL, 0x7Eu, 0xB6u)) {
        return BMI088_BUS_ERROR;
    }
    bus.delay_ms(bus.context, 50u);
    if (!write_retry(&bus, BMI088_GYRO, 0x14u, 0xB6u)) {
        return BMI088_BUS_ERROR;
    }
    bus.delay_ms(bus.context, 30u);

    if (!read_retry(&bus, BMI088_ACCEL, BMI088_ACCEL_CHIP_ID_REG,
                    &found_accel_id, 1u) ||
        !read_retry(&bus, BMI088_GYRO, BMI088_GYRO_CHIP_ID_REG,
                    &found_gyro_id, 1u)) {
        return BMI088_BUS_ERROR;
    }
    if (accel_id != NULL) {
        *accel_id = found_accel_id;
    }
    if (gyro_id != NULL) {
        *gyro_id = found_gyro_id;
    }
    if (found_accel_id != BMI088_ACCEL_CHIP_ID ||
        found_gyro_id != BMI088_GYRO_CHIP_ID) {
        return BMI088_BAD_ID;
    }

    bmi088_result_t result =
        apply_config(&bus, accel_config,
                     sizeof accel_config / sizeof accel_config[0]);
    if (result == BMI088_OK) {
        result = apply_config(&bus, gyro_config,
                              sizeof gyro_config / sizeof gyro_config[0]);
    }
    if (result != BMI088_OK) {
        return result;
    }

    self->bus = bus;
    self->axis_map = axis_map;
    return BMI088_OK;
}

/**
 * @brief 按小端字节序解析一个有符号 16 位采样值。
 * @param data 指向至少两个字节的小端数据。
 * @return 解析后的有符号 16 位整数。
 */
static int16_t read_i16_le(const uint8_t *data)
{
    return (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

/**
 * @brief 读取并完成缩放、轴置换及符号变换。
 * @param self 已初始化的 BMI088 驱动实例。
 * @param target 目标器件。
 * @param reg 三轴数据起始寄存器。
 * @param scale 原始计数到物理单位的缩放因子。
 * @param raw 原始三轴数据输出指针。
 * @param converted 机体坐标系下的转换结果输出指针。
 * @return 读取和转换结果。
 */
static bmi088_result_t read_sample(bmi088_t *self, bmi088_target_t target,
                                   uint8_t reg, float scale,
                                   bmi088_raw_sample_t *raw,
                                   imu_vec3f_t *converted)
{
    uint8_t data[6];
    float sensor[3];

    if (self == NULL || raw == NULL || converted == NULL ||
        self->bus.read == NULL ||
        !bmi088_axis_map_is_right_handed(&self->axis_map)) {
        return BMI088_BAD_ARGUMENT;
    }
    float *const body[3] = {&converted->x, &converted->y, &converted->z};
    if (!read_retry(&self->bus, target, reg, data, sizeof data)) {
        return BMI088_BUS_ERROR;
    }

    raw->x = read_i16_le(&data[0]);
    raw->y = read_i16_le(&data[2]);
    raw->z = read_i16_le(&data[4]);
    sensor[0] = (float)raw->x * scale;
    sensor[1] = (float)raw->y * scale;
    sensor[2] = (float)raw->z * scale;
    for (size_t i = 0u; i < 3u; ++i) {
        *body[i] = sensor[self->axis_map.source_axis[i]] *
                   (float)self->axis_map.sign[i];
    }
    return BMI088_OK;
}

bmi088_result_t bmi088_read_accel(bmi088_t *self, bmi088_raw_sample_t *raw,
                                  imu_vec3f_t *body_g)
{
    return read_sample(self, BMI088_ACCEL, BMI088_ACCEL_DATA_REG,
                       6.0f / 32768.0f, raw, body_g);
}

bmi088_result_t bmi088_read_gyro(bmi088_t *self, bmi088_raw_sample_t *raw,
                                 imu_vec3f_t *body_dps)
{
    return read_sample(self, BMI088_GYRO, BMI088_GYRO_DATA_REG,
                       2000.0f / 32768.0f, raw, body_dps);
}
