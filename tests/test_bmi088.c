#include "bmi088.h"
#include "fake_bmi088_bus.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bmi088_target_t target;
    uint8_t reg;
    uint8_t value;
} register_value_t;

static const register_value_t accel_config[] = {
    {BMI088_ACCEL, 0x7Cu, 0x00u},
    {BMI088_ACCEL, 0x7Du, 0x04u},
    {BMI088_ACCEL, 0x40u, 0xABu},
    {BMI088_ACCEL, 0x41u, 0x01u},
    {BMI088_ACCEL, 0x53u, 0x08u},
    {BMI088_ACCEL, 0x58u, 0x04u},
};

static const register_value_t gyro_config[] = {
    {BMI088_GYRO, 0x0Fu, 0x00u},
    {BMI088_GYRO, 0x10u, 0x02u},
    {BMI088_GYRO, 0x15u, 0x80u},
    {BMI088_GYRO, 0x16u, 0x00u},
    {BMI088_GYRO, 0x18u, 0x01u},
};

static void script_byte(fake_bmi088_bus_t *fake, bmi088_target_t target,
                        uint8_t reg, uint8_t value)
{
    fake_bmi088_script_read(fake, target, reg, &value, 1u, true);
}

static void script_init_through(fake_bmi088_bus_t *fake,
                                bmi088_target_t stop_target, uint8_t stop_reg,
                                uint8_t stop_value, unsigned stop_repeats)
{
    script_byte(fake, BMI088_ACCEL, 0x00u, 0x1Eu);
    script_byte(fake, BMI088_ACCEL, 0x00u, 0x1Eu);
    script_byte(fake, BMI088_GYRO, 0x00u, 0x0Fu);

    for (size_t i = 0; i < sizeof accel_config / sizeof accel_config[0]; ++i) {
        const register_value_t item = accel_config[i];
        if (item.target == stop_target && item.reg == stop_reg) {
            for (unsigned repeat = 0; repeat < stop_repeats; ++repeat) {
                script_byte(fake, item.target, item.reg, stop_value);
            }
            return;
        }
        script_byte(fake, item.target, item.reg, item.value);
    }
    for (size_t i = 0; i < sizeof gyro_config / sizeof gyro_config[0]; ++i) {
        const register_value_t item = gyro_config[i];
        if (item.target == stop_target && item.reg == stop_reg) {
            for (unsigned repeat = 0; repeat < stop_repeats; ++repeat) {
                script_byte(fake, item.target, item.reg, stop_value);
            }
            return;
        }
        script_byte(fake, item.target, item.reg, item.value);
    }
}

static void script_successful_init(fake_bmi088_bus_t *fake)
{
    script_init_through(fake, BMI088_ACCEL, 0xFFu, 0u, 0u);
}

static void assert_transaction(const fake_bmi088_bus_t *fake, size_t index,
                               fake_bmi088_transaction_kind_t kind,
                               bmi088_target_t target, uint8_t reg,
                               uint8_t value, uint32_t delay_ms)
{
    assert(index < fake->transaction_count);
    const fake_bmi088_transaction_t *const transaction =
        &fake->transactions[index];
    assert(transaction->kind == kind);
    assert(transaction->success);
    if (kind == FAKE_BMI088_DELAY) {
        assert(transaction->delay_ms == delay_ms);
    } else {
        assert(transaction->target == target);
        assert(transaction->reg == reg);
        assert(transaction->length == 1u);
        if (kind == FAKE_BMI088_WRITE) {
            assert(transaction->value == value);
        }
    }
}

static void test_axis_map_rejects_every_non_right_handed_shape(void)
{
    assert(bmi088_axis_map_is_right_handed(
        &(bmi088_axis_map_t){{0u, 1u, 2u}, {1, 1, 1}}));
    assert(!bmi088_axis_map_is_right_handed(
        &(bmi088_axis_map_t){{1u, 0u, 2u}, {1, 1, 1}}));
    assert(bmi088_axis_map_is_right_handed(
        &(bmi088_axis_map_t){{1u, 0u, 2u}, {1, -1, 1}}));
    assert(!bmi088_axis_map_is_right_handed(
        &(bmi088_axis_map_t){{0u, 0u, 2u}, {1, -1, 1}}));
    assert(!bmi088_axis_map_is_right_handed(
        &(bmi088_axis_map_t){{0u, 1u, 3u}, {1, 1, 1}}));
    assert(!bmi088_axis_map_is_right_handed(
        &(bmi088_axis_map_t){{0u, 1u, 2u}, {1, 0, 1}}));
    assert(!bmi088_axis_map_is_right_handed(NULL));
    assert(bmi088_axis_map_is_right_handed(&BMI088_AXIS_MAP));
}

static void test_init_uses_exact_register_order_readbacks_and_delays(void)
{
    fake_bmi088_bus_t fake;
    bmi088_t sensor;
    uint8_t accel_id = 0u;
    uint8_t gyro_id = 0u;
    fake_bmi088_bus_init(&fake);
    script_successful_init(&fake);

    assert(bmi088_init(&sensor, fake_bmi088_bus_interface(&fake),
                       BMI088_AXIS_MAP, &accel_id, &gyro_id) == BMI088_OK);
    assert(accel_id == 0x1Eu);
    assert(gyro_id == 0x0Fu);
    assert(!fake.overflow && !fake.script_mismatch);
    assert(fake.read_index == fake.read_count);
    assert(fake.transaction_count == 32u);

    size_t transaction = 0u;
    assert_transaction(&fake, transaction++, FAKE_BMI088_READ, BMI088_ACCEL,
                       0x00u, 0u, 0u);
    assert_transaction(&fake, transaction++, FAKE_BMI088_DELAY, BMI088_ACCEL,
                       0u, 0u, 1u);
    assert_transaction(&fake, transaction++, FAKE_BMI088_WRITE, BMI088_ACCEL,
                       0x7Eu, 0xB6u, 0u);
    assert_transaction(&fake, transaction++, FAKE_BMI088_DELAY, BMI088_ACCEL,
                       0u, 0u, 50u);
    assert_transaction(&fake, transaction++, FAKE_BMI088_WRITE, BMI088_GYRO,
                       0x14u, 0xB6u, 0u);
    assert_transaction(&fake, transaction++, FAKE_BMI088_DELAY, BMI088_ACCEL,
                       0u, 0u, 30u);
    assert_transaction(&fake, transaction++, FAKE_BMI088_READ, BMI088_ACCEL,
                       0x00u, 0u, 0u);
    assert_transaction(&fake, transaction++, FAKE_BMI088_READ, BMI088_GYRO,
                       0x00u, 0u, 0u);

    for (size_t i = 0; i < sizeof accel_config / sizeof accel_config[0]; ++i) {
        const register_value_t item = accel_config[i];
        assert_transaction(&fake, transaction++, FAKE_BMI088_WRITE,
                           item.target, item.reg, item.value, 0u);
        if (item.reg == 0x7Cu || item.reg == 0x7Du) {
            assert_transaction(&fake, transaction++, FAKE_BMI088_DELAY,
                               BMI088_ACCEL, 0u, 0u, 5u);
        }
        assert_transaction(&fake, transaction++, FAKE_BMI088_READ,
                           item.target, item.reg, 0u, 0u);
    }
    for (size_t i = 0; i < sizeof gyro_config / sizeof gyro_config[0]; ++i) {
        const register_value_t item = gyro_config[i];
        assert_transaction(&fake, transaction++, FAKE_BMI088_WRITE,
                           item.target, item.reg, item.value, 0u);
        assert_transaction(&fake, transaction++, FAKE_BMI088_READ,
                           item.target, item.reg, 0u, 0u);
    }
    assert(transaction == fake.transaction_count);
}

static void test_each_bus_operation_stops_after_three_attempts(void)
{
    fake_bmi088_bus_t fake;
    bmi088_t sensor;
    fake_bmi088_bus_init(&fake);
    script_successful_init(&fake);
    fake_bmi088_fail_next(&fake, 2u);

    assert(bmi088_init(&sensor, fake_bmi088_bus_interface(&fake),
                       BMI088_AXIS_MAP, NULL, NULL) == BMI088_OK);
    assert(fake.transaction_count == 34u);
    assert(fake.transactions[0].kind == FAKE_BMI088_READ);
    assert(!fake.transactions[0].success);
    assert(!fake.transactions[1].success);
    assert(fake.transactions[2].success);

    fake_bmi088_bus_init(&fake);
    fake_bmi088_fail_next(&fake, 3u);
    assert(bmi088_init(&sensor, fake_bmi088_bus_interface(&fake),
                       BMI088_AXIS_MAP, NULL, NULL) == BMI088_BUS_ERROR);
    assert(fake.transaction_count == 3u);
    assert(fake.transactions[0].kind == FAKE_BMI088_READ);
    assert(fake.transactions[1].kind == FAKE_BMI088_READ);
    assert(fake.transactions[2].kind == FAKE_BMI088_READ);
}

static void test_wrong_identity_and_readback_have_distinct_results(void)
{
    fake_bmi088_bus_t fake;
    bmi088_t sensor;
    uint8_t accel_id = 0u;
    fake_bmi088_bus_init(&fake);
    script_byte(&fake, BMI088_ACCEL, 0x00u, 0x00u);
    script_byte(&fake, BMI088_ACCEL, 0x00u, 0x00u);
    script_byte(&fake, BMI088_GYRO, 0x00u, 0x0Fu);
    assert(bmi088_init(&sensor, fake_bmi088_bus_interface(&fake),
                       BMI088_AXIS_MAP, &accel_id, NULL) == BMI088_BAD_ID);
    assert(accel_id == 0x00u);

    fake_bmi088_bus_init(&fake);
    script_init_through(&fake, BMI088_ACCEL, 0x40u, 0xAAu, 3u);
    assert(bmi088_init(&sensor, fake_bmi088_bus_interface(&fake),
                       BMI088_AXIS_MAP, NULL, NULL) == BMI088_VERIFY_ERROR);
    assert(!fake.script_mismatch);
    const size_t count = fake.transaction_count;
    assert(count >= 6u);
    for (size_t i = count - 6u; i < count; i += 2u) {
        assert(fake.transactions[i].kind == FAKE_BMI088_WRITE);
        assert(fake.transactions[i].reg == 0x40u);
        assert(fake.transactions[i + 1u].kind == FAKE_BMI088_READ);
        assert(fake.transactions[i + 1u].reg == 0x40u);
    }
}

static void assert_close(float actual, float expected)
{
    assert(fabsf(actual - expected) < 1e-5f);
}

static void test_raw_bursts_are_little_endian_and_converted_after_mapping(void)
{
    static const uint8_t raw_bytes[] = {
        0x00u, 0x40u, 0x00u, 0xC0u, 0x00u, 0x20u,
    };
    fake_bmi088_bus_t fake;
    bmi088_t sensor;
    bmi088_raw_sample_t raw;
    imu_vec3f_t converted;
    fake_bmi088_bus_init(&fake);
    sensor = (bmi088_t){
        .bus = fake_bmi088_bus_interface(&fake),
        .axis_map = BMI088_AXIS_MAP,
    };

    fake_bmi088_script_read(&fake, BMI088_ACCEL, 0x12u, raw_bytes,
                            sizeof raw_bytes, true);
    assert(bmi088_read_accel(&sensor, &raw, &converted) == BMI088_OK);
    assert(raw.x == 16384 && raw.y == -16384 && raw.z == 8192);
    assert_close(converted.x, 3.0f);
    assert_close(converted.y, -3.0f);
    assert_close(converted.z, 1.5f);

    fake_bmi088_script_read(&fake, BMI088_GYRO, 0x02u, raw_bytes,
                            sizeof raw_bytes, true);
    assert(bmi088_read_gyro(&sensor, &raw, &converted) == BMI088_OK);
    assert(raw.x == 16384 && raw.y == -16384 && raw.z == 8192);
    assert_close(converted.x, 1000.0f);
    assert_close(converted.y, -1000.0f);
    assert_close(converted.z, 500.0f);
    assert(fake.transaction_count == 2u);
    assert(fake.transactions[0].length == 6u);
    assert(fake.transactions[1].length == 6u);
}

static void test_sample_read_retries_and_applies_configured_axis_map(void)
{
    static const uint8_t raw_bytes[] = {
        0x00u, 0x40u, 0x00u, 0xC0u, 0x00u, 0x20u,
    };
    fake_bmi088_bus_t fake;
    bmi088_t sensor;
    bmi088_raw_sample_t raw;
    imu_vec3f_t converted;
    fake_bmi088_bus_init(&fake);
    sensor = (bmi088_t){
        .bus = fake_bmi088_bus_interface(&fake),
        .axis_map = {{1u, 0u, 2u}, {1, -1, 1}},
    };
    fake_bmi088_script_read(&fake, BMI088_ACCEL, 0x12u, raw_bytes,
                            sizeof raw_bytes, true);
    fake_bmi088_fail_next(&fake, 2u);
    assert(bmi088_read_accel(&sensor, &raw, &converted) == BMI088_OK);
    assert(fake.transaction_count == 3u);
    assert_close(converted.x, -3.0f);
    assert_close(converted.y, -3.0f);
    assert_close(converted.z, 1.5f);

    fake_bmi088_bus_init(&fake);
    sensor.bus = fake_bmi088_bus_interface(&fake);
    fake_bmi088_fail_next(&fake, 3u);
    assert(bmi088_read_accel(&sensor, &raw, &converted) ==
           BMI088_BUS_ERROR);
    assert(fake.transaction_count == 3u);
}

static void test_invalid_arguments_and_map_do_not_touch_the_bus(void)
{
    fake_bmi088_bus_t fake;
    bmi088_t sensor;
    bmi088_bus_t bus;
    fake_bmi088_bus_init(&fake);
    bus = fake_bmi088_bus_interface(&fake);
    assert(bmi088_init(NULL, bus, BMI088_AXIS_MAP, NULL, NULL) ==
           BMI088_BAD_ARGUMENT);
    assert(bmi088_init(&sensor, (bmi088_bus_t){0}, BMI088_AXIS_MAP, NULL,
                       NULL) == BMI088_BAD_ARGUMENT);
    assert(bmi088_init(&sensor, bus,
                       (bmi088_axis_map_t){{0u, 0u, 2u}, {1, 1, 1}},
                       NULL, NULL) == BMI088_BAD_AXIS_MAP);
    assert(fake.transaction_count == 0u);
}

int main(void)
{
    test_axis_map_rejects_every_non_right_handed_shape();
    test_init_uses_exact_register_order_readbacks_and_delays();
    test_each_bus_operation_stops_after_three_attempts();
    test_wrong_identity_and_readback_have_distinct_results();
    test_raw_bursts_are_little_endian_and_converted_after_mapping();
    test_sample_read_retries_and_applies_configured_axis_map();
    test_invalid_arguments_and_map_do_not_touch_the_bus();
    return 0;
}
