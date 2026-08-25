#ifndef FAKE_BMI088_BUS_H
#define FAKE_BMI088_BUS_H

#include "bmi088.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FAKE_BMI088_MAX_TRANSACTIONS 128u
#define FAKE_BMI088_MAX_READS 64u
#define FAKE_BMI088_MAX_READ_LENGTH 8u

typedef enum {
    FAKE_BMI088_READ,
    FAKE_BMI088_WRITE,
    FAKE_BMI088_DELAY
} fake_bmi088_transaction_kind_t;

typedef struct {
    fake_bmi088_transaction_kind_t kind;
    bmi088_target_t target;
    uint8_t reg;
    uint8_t value;
    size_t length;
    uint32_t delay_ms;
    bool success;
} fake_bmi088_transaction_t;

typedef struct {
    bmi088_target_t target;
    uint8_t reg;
    uint8_t data[FAKE_BMI088_MAX_READ_LENGTH];
    size_t length;
    bool success;
} fake_bmi088_scripted_read_t;

typedef struct {
    fake_bmi088_transaction_t transactions[FAKE_BMI088_MAX_TRANSACTIONS];
    size_t transaction_count;
    fake_bmi088_scripted_read_t reads[FAKE_BMI088_MAX_READS];
    size_t read_count;
    size_t read_index;
    unsigned fail_next_operations;
    bool overflow;
    bool script_mismatch;
} fake_bmi088_bus_t;

void fake_bmi088_bus_init(fake_bmi088_bus_t *fake);
bmi088_bus_t fake_bmi088_bus_interface(fake_bmi088_bus_t *fake);
void fake_bmi088_fail_next(fake_bmi088_bus_t *fake, unsigned count);
void fake_bmi088_script_read(fake_bmi088_bus_t *fake,
                             bmi088_target_t target, uint8_t reg,
                             const uint8_t *data, size_t length, bool success);

#endif
