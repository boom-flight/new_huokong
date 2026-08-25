#include "fake_bmi088_bus.h"

#include <string.h>

static fake_bmi088_transaction_t *record(fake_bmi088_bus_t *fake)
{
    if (fake->transaction_count >= FAKE_BMI088_MAX_TRANSACTIONS) {
        fake->overflow = true;
        return NULL;
    }
    return &fake->transactions[fake->transaction_count++];
}

static bool fake_read(void *context, bmi088_target_t target, uint8_t reg,
                      uint8_t *data, size_t length)
{
    fake_bmi088_bus_t *const fake = context;
    fake_bmi088_transaction_t *const transaction = record(fake);
    bool success = false;

    if (fake->fail_next_operations > 0u) {
        --fake->fail_next_operations;
    } else if (fake->read_index >= fake->read_count) {
        fake->script_mismatch = true;
    } else {
        const fake_bmi088_scripted_read_t *const scripted =
            &fake->reads[fake->read_index++];
        if (scripted->target != target || scripted->reg != reg ||
            scripted->length != length) {
            fake->script_mismatch = true;
        } else {
            success = scripted->success;
            if (success) {
                memcpy(data, scripted->data, length);
            }
        }
    }

    if (transaction != NULL) {
        *transaction = (fake_bmi088_transaction_t){
            .kind = FAKE_BMI088_READ,
            .target = target,
            .reg = reg,
            .length = length,
            .success = success,
        };
    }
    return success;
}

static bool fake_write(void *context, bmi088_target_t target, uint8_t reg,
                       uint8_t value)
{
    fake_bmi088_bus_t *const fake = context;
    fake_bmi088_transaction_t *const transaction = record(fake);
    bool success = true;

    if (fake->fail_next_operations > 0u) {
        --fake->fail_next_operations;
        success = false;
    }
    if (transaction != NULL) {
        *transaction = (fake_bmi088_transaction_t){
            .kind = FAKE_BMI088_WRITE,
            .target = target,
            .reg = reg,
            .value = value,
            .length = 1u,
            .success = success,
        };
    }
    return success;
}

static void fake_delay_ms(void *context, uint32_t delay_ms)
{
    fake_bmi088_bus_t *const fake = context;
    fake_bmi088_transaction_t *const transaction = record(fake);

    if (transaction != NULL) {
        *transaction = (fake_bmi088_transaction_t){
            .kind = FAKE_BMI088_DELAY,
            .delay_ms = delay_ms,
            .success = true,
        };
    }
}

void fake_bmi088_bus_init(fake_bmi088_bus_t *fake)
{
    *fake = (fake_bmi088_bus_t){0};
}

bmi088_bus_t fake_bmi088_bus_interface(fake_bmi088_bus_t *fake)
{
    return (bmi088_bus_t){
        .context = fake,
        .read = fake_read,
        .write = fake_write,
        .delay_ms = fake_delay_ms,
    };
}

void fake_bmi088_fail_next(fake_bmi088_bus_t *fake, unsigned count)
{
    fake->fail_next_operations = count;
}

void fake_bmi088_script_read(fake_bmi088_bus_t *fake,
                             bmi088_target_t target, uint8_t reg,
                             const uint8_t *data, size_t length, bool success)
{
    if (fake->read_count >= FAKE_BMI088_MAX_READS ||
        length > FAKE_BMI088_MAX_READ_LENGTH) {
        fake->overflow = true;
        return;
    }

    fake_bmi088_scripted_read_t *const scripted =
        &fake->reads[fake->read_count++];
    *scripted = (fake_bmi088_scripted_read_t){
        .target = target,
        .reg = reg,
        .length = length,
        .success = success,
    };
    if (data != NULL) {
        memcpy(scripted->data, data, length);
    }
}
