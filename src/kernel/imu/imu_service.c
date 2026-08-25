#include "imu/imu_service.h"

#include "attitude/imu_calibration.h"
#include "attitude/mahony.h"
#include "bmi088/bmi088.h"
#include "devices/bmi088_stm32.h"
#include "imu/imu_policy.h"
#include "time/monotonic_clock_stm32.h"
#include "transport/telemetry_uart_stm32.h"

#include <board.h>
#include <drv_gpio.h>
#include <rtthread.h>

#include <stddef.h>
#include <stdint.h>

#define DEG_TO_RAD 0.01745329251994329577f
#define IMU_THREAD_PRIORITY 5u
#define IMU_THREAD_STACK_SIZE 768u
#define IMU_EVENT_ACCEL (1u << 0)
#define IMU_EVENT_GYRO (1u << 1)
#define STATE_LED_PIN GET_PIN(B, 6)

typedef struct {
    bmi088_t sensor;
    mahony_t estimator;
    imu_calibration_t calibration;
    imu_snapshot_t snapshot;
    imu_state_t state;
    imu_vec3f_t newest_accel_g;
    uint32_t newest_accel_timestamp_us;
    uint32_t consumed_accel_sequence;
    uint32_t consumed_gyro_sequence;
    uint32_t gyro_baseline_us;
    uint32_t last_gyro_timestamp_us;
    rt_tick_t state_entered_tick;
    rt_tick_t led_deadline;
    rt_tick_t diagnostics_deadline;
    uint16_t state_conditions;
    uint16_t accel_conditions;
    uint16_t gyro_conditions;
    uint8_t consecutive_read_failures;
    uint8_t led_phase;
    bool newest_accel_valid;
    bool gyro_baseline_valid;
    bool last_gyro_timestamp_valid;
    bool gyro_expired;
    bool led_output_valid;
    bool led_on;
    bool ids_logged;
    bool calibration_logged;
} imu_runtime_t;

static struct rt_event imu_event;
static struct rt_thread imu_thread;
rt_align(RT_ALIGN_SIZE)
static rt_uint8_t imu_stack[IMU_THREAD_STACK_SIZE];
static imu_snapshot_t snapshots[2];
static volatile uint8_t active_snapshot;
static uint32_t telemetry_drop_count;
static bool service_started;

static void publish_runtime_snapshot(imu_runtime_t *runtime);
static bool service_gyro_expiry(imu_runtime_t *runtime, uint32_t now_us);

static void notify_data_ready(void *context, uint32_t event_mask)
{
    struct rt_event *event = context;
    uint32_t events = 0u;

    if ((event_mask & BMI088_DRDY_EVENT_ACCEL) != 0u) {
        events |= IMU_EVENT_ACCEL;
    }
    if ((event_mask & BMI088_DRDY_EVENT_GYRO) != 0u) {
        events |= IMU_EVENT_GYRO;
    }
    if (events != 0u) {
        (void)rt_event_send(event, events);
    }
}

static bool tick_reached(rt_tick_t now, rt_tick_t deadline)
{
    return (rt_int32_t)(now - deadline) >= 0;
}

static const char *state_name(imu_state_t state)
{
    switch (state) {
    case IMU_INITIALIZING:
        return "initializing";
    case IMU_CALIBRATING:
        return "calibrating";
    case IMU_RUNNING:
        return "running";
    case IMU_FAULT_RETRY:
        return "fault-retry";
    default:
        return "unknown";
    }
}

static void update_state_led(imu_runtime_t *runtime, rt_tick_t now)
{
    while (tick_reached(now, runtime->led_deadline)) {
        const imu_led_step_t step =
            imu_led_step(runtime->state, runtime->led_phase);

        if (!runtime->led_output_valid || runtime->led_on != step.on) {
            rt_pin_write(STATE_LED_PIN, step.on ? PIN_HIGH : PIN_LOW);
            runtime->led_output_valid = true;
            runtime->led_on = step.on;
        }
        runtime->led_phase = step.next_phase;
        runtime->led_deadline += rt_tick_from_millisecond(step.duration_ms);
    }
}

static void log_initial_state(imu_runtime_t *runtime)
{
    const rt_tick_t now = rt_tick_get();

    runtime->state_entered_tick = now;
    runtime->led_deadline = now;
    runtime->diagnostics_deadline = now + RT_TICK_PER_SECOND;
    runtime->led_phase = 0u;
    rt_kprintf("IMU state: %s\n", state_name(runtime->state));
    update_state_led(runtime, now);
}

static void transition_state(imu_runtime_t *runtime, imu_state_t next)
{
    const imu_state_t previous = runtime->state;
    const rt_tick_t now = rt_tick_get();

    if (previous == next) {
        return;
    }
    runtime->state = next;
    runtime->state_entered_tick = now;
    runtime->led_deadline = now;
    runtime->led_phase = 0u;
    rt_kprintf("IMU state: %s -> %s\n", state_name(previous),
               state_name(next));
    update_state_led(runtime, now);
}

static void log_ids_once(imu_runtime_t *runtime, uint8_t accel_id,
                         uint8_t gyro_id)
{
    if (runtime->ids_logged) {
        return;
    }
    runtime->ids_logged = true;
    rt_kprintf("BMI088 IDs: accel=0x%02x gyro=0x%02x\n",
               (unsigned)accel_id, (unsigned)gyro_id);
}

static void log_calibration_complete_once(imu_runtime_t *runtime)
{
    if (runtime->calibration_logged) {
        return;
    }
    runtime->calibration_logged = true;
    rt_kprintf("IMU calibration complete\n");
}

static uint32_t telemetry_drops_read(void)
{
    const rt_base_t level = rt_hw_interrupt_disable();
    const uint32_t drops = telemetry_drop_count;

    rt_hw_interrupt_enable(level);
    return drops;
}

static void log_diagnostics_if_due(imu_runtime_t *runtime, rt_tick_t now)
{
    const imu_diagnostics_t *diagnostics = &runtime->snapshot.diagnostics;

    if (!tick_reached(now, runtime->diagnostics_deadline)) {
        return;
    }
    runtime->diagnostics_deadline = now + RT_TICK_PER_SECOND;
    rt_kprintf("IMU errors: spi=%u accel_overrun=%u gyro_overrun=%u "
               "rejected_dt=%u long_gap=%u telemetry_drop=%u reinit=%u\n",
               (unsigned)diagnostics->spi_errors,
               (unsigned)diagnostics->accel_overruns,
               (unsigned)diagnostics->gyro_overruns,
               (unsigned)diagnostics->rejected_dt,
               (unsigned)diagnostics->long_gaps,
               (unsigned)telemetry_drops_read(),
               (unsigned)diagnostics->sensor_reinitializations);
}

static void service_housekeeping(imu_runtime_t *runtime)
{
    const rt_tick_t now = rt_tick_get();

    if (service_gyro_expiry(runtime, monotonic_clock_stm32_now_us())) {
        publish_runtime_snapshot(runtime);
    }
    update_state_led(runtime, now);
    log_diagnostics_if_due(runtime, now);
}

static rt_tick_t housekeeping_wait(const imu_runtime_t *runtime)
{
    const rt_tick_t now = rt_tick_get();
    rt_tick_t wait = (rt_tick_t)imu_housekeeping_wait_ticks(
        (uint32_t)now, (uint32_t)runtime->led_deadline,
        (uint32_t)runtime->diagnostics_deadline);
    const bmi088_drdy_latch_t gyro_latch = bmi088_stm32_gyro_latch();

    if (runtime->state == IMU_RUNNING && !runtime->gyro_expired &&
        gyro_latch.sequence == runtime->consumed_gyro_sequence) {
        const uint32_t delay_us = imu_gyro_expiry_delay_us(
            runtime->last_gyro_timestamp_valid,
            monotonic_clock_stm32_now_us(),
            runtime->last_gyro_timestamp_us);

        if (delay_us != UINT32_MAX) {
            const rt_tick_t expiry_wait = rt_tick_from_millisecond(
                (rt_int32_t)((delay_us + 999u) / 1000u));
            if (expiry_wait < wait) {
                wait = expiry_wait;
            }
        }
    }
    return wait;
}

static void increment_saturating(uint32_t *value)
{
    if (*value != UINT32_MAX) {
        ++*value;
    }
}

static imu_vec3f_t scale_vec(imu_vec3f_t value, float scale)
{
    return (imu_vec3f_t){value.x * scale, value.y * scale, value.z * scale};
}

static void update_attitude_fields(imu_runtime_t *runtime)
{
    runtime->snapshot.quaternion = mahony_quaternion(&runtime->estimator);
    runtime->snapshot.euler_deg = mahony_euler_deg(&runtime->estimator);
}

static void publish_snapshot(const imu_snapshot_t *value)
{
    const uint8_t next = (uint8_t)(active_snapshot ^ 1u);
    rt_base_t level;

    snapshots[next] = *value;
    level = rt_hw_interrupt_disable();
    snapshots[next].diagnostics.telemetry_drops = telemetry_drop_count;
    active_snapshot = next;
    rt_hw_interrupt_enable(level);
}

static void publish_runtime_snapshot(imu_runtime_t *runtime)
{
    runtime->snapshot.status =
        imu_service_status(runtime->state, runtime->estimator.valid,
                           runtime->state_conditions,
                           runtime->accel_conditions,
                           runtime->gyro_conditions);
    update_attitude_fields(runtime);
    publish_snapshot(&runtime->snapshot);
}

static bool service_gyro_expiry(imu_runtime_t *runtime, uint32_t now_us)
{
    const bmi088_drdy_latch_t gyro_latch = bmi088_stm32_gyro_latch();

    return imu_gyro_expiry_transition(
        runtime->state,
        gyro_latch.sequence != runtime->consumed_gyro_sequence,
        now_us, runtime->last_gyro_timestamp_valid,
        runtime->last_gyro_timestamp_us, &runtime->gyro_expired,
        &runtime->gyro_conditions, &runtime->estimator);
}

bool imu_snapshot_read(imu_snapshot_t *out)
{
    rt_base_t level;

    if (out == NULL) {
        return false;
    }
    level = rt_hw_interrupt_disable();
    *out = snapshots[active_snapshot];
    rt_hw_interrupt_enable(level);
    return true;
}

void imu_service_record_telemetry_drop(void)
{
    const rt_base_t level = rt_hw_interrupt_disable();

    if (telemetry_drop_count != UINT32_MAX) {
        ++telemetry_drop_count;
    }
    rt_hw_interrupt_enable(level);
}

static bool record_overrun(uint32_t sequence, uint32_t consumed_sequence,
                           uint32_t *overrun_count)
{
    if ((uint32_t)(sequence - consumed_sequence) <= 1u) {
        return false;
    }
    increment_saturating(overrun_count);
    return true;
}

static void publish_read_failure(imu_runtime_t *runtime, uint32_t timestamp_us)
{
    increment_saturating(&runtime->snapshot.diagnostics.spi_errors);
    if (runtime->consecutive_read_failures != UINT8_MAX) {
        ++runtime->consecutive_read_failures;
    }
    if (runtime->state == IMU_CALIBRATING) {
        imu_calibration_init(&runtime->calibration);
    }

    runtime->snapshot.timestamp_us = timestamp_us;
    if (runtime->consecutive_read_failures >= 3u) {
        mahony_invalidate(&runtime->estimator);
        runtime->newest_accel_valid = false;
        runtime->gyro_baseline_valid = false;
        transition_state(runtime, IMU_FAULT_RETRY);
    }
    publish_runtime_snapshot(runtime);
}

static void reset_after_sensor_init(imu_runtime_t *runtime)
{
    const imu_diagnostics_t diagnostics = runtime->snapshot.diagnostics;
    const imu_vec3f_t last_accel_g = runtime->snapshot.accel_g;
    const bmi088_drdy_latch_t accel_latch = bmi088_stm32_accel_latch();
    const bmi088_drdy_latch_t gyro_latch = bmi088_stm32_gyro_latch();

    runtime->estimator = (mahony_t){
        .q = {1.0f, 0.0f, 0.0f, 0.0f},
    };
    imu_calibration_init(&runtime->calibration);
    runtime->snapshot = (imu_snapshot_t){
        .quaternion = {1.0f, 0.0f, 0.0f, 0.0f},
        .accel_g = last_accel_g,
        .diagnostics = diagnostics,
    };
    runtime->newest_accel_g = (imu_vec3f_t){0.0f, 0.0f, 0.0f};
    runtime->newest_accel_timestamp_us = 0u;
    runtime->consumed_accel_sequence = accel_latch.sequence;
    runtime->consumed_gyro_sequence = gyro_latch.sequence;
    runtime->gyro_baseline_us = 0u;
    runtime->last_gyro_timestamp_us = 0u;
    runtime->state_conditions = 0u;
    runtime->accel_conditions = IMU_STATUS_ACCEL_CORRECTION_INVALID;
    runtime->gyro_conditions = IMU_STATUS_TIMESTAMP_INVALID;
    runtime->consecutive_read_failures = 0u;
    runtime->newest_accel_valid = false;
    runtime->gyro_baseline_valid = false;
    runtime->last_gyro_timestamp_valid = false;
    runtime->gyro_expired = false;
    transition_state(runtime, IMU_CALIBRATING);
    runtime->snapshot.timestamp_us = monotonic_clock_stm32_now_us();
    publish_runtime_snapshot(runtime);
}

static void initialize_sensor(imu_runtime_t *runtime)
{
    uint8_t accel_id = 0u;
    uint8_t gyro_id = 0u;
    const bmi088_result_t result =
        bmi088_init(&runtime->sensor, bmi088_stm32_bus(), BMI088_AXIS_MAP,
                    &accel_id, &gyro_id);

    if (result != BMI088_OK) {
        mahony_invalidate(&runtime->estimator);
        runtime->newest_accel_valid = false;
        runtime->gyro_baseline_valid = false;
        transition_state(runtime, IMU_FAULT_RETRY);
        runtime->state_conditions = IMU_STATUS_BMI_INIT_FAILED;
        runtime->accel_conditions = 0u;
        runtime->gyro_conditions = IMU_STATUS_TIMESTAMP_INVALID;
        runtime->snapshot.timestamp_us = monotonic_clock_stm32_now_us();
        publish_runtime_snapshot(runtime);
        return;
    }

    log_ids_once(runtime, accel_id, gyro_id);
    reset_after_sensor_init(runtime);
}

static void process_accel(imu_runtime_t *runtime)
{
    const bmi088_drdy_latch_t latch = bmi088_stm32_accel_latch();
    bmi088_raw_sample_t raw;
    imu_vec3f_t accel_g;

    runtime->accel_conditions = 0u;

    if (record_overrun(latch.sequence, runtime->consumed_accel_sequence,
                       &runtime->snapshot.diagnostics.accel_overruns)) {
        runtime->accel_conditions |= IMU_STATUS_EVENT_OVERRUN;
        if (runtime->state == IMU_CALIBRATING) {
            imu_calibration_init(&runtime->calibration);
        }
    }

    const bmi088_result_t result =
        bmi088_read_accel(&runtime->sensor, &raw, &accel_g);
    runtime->consumed_accel_sequence = latch.sequence;
    if (result != BMI088_OK) {
        runtime->newest_accel_valid = false;
        runtime->accel_conditions |= IMU_STATUS_ACCEL_CORRECTION_INVALID |
                                     IMU_STATUS_SPI_ERROR;
        publish_read_failure(runtime, latch.timestamp_us);
        return;
    }

    runtime->consecutive_read_failures = 0u;
    runtime->newest_accel_g = accel_g;
    runtime->newest_accel_timestamp_us = latch.timestamp_us;
    runtime->newest_accel_valid = true;
    runtime->snapshot.timestamp_us = latch.timestamp_us;
    runtime->snapshot.accel_g = accel_g;
    increment_saturating(&runtime->snapshot.diagnostics.accel_samples);
    if (!imu_accel_correction_valid(accel_g, latch.sequence, latch.sequence,
                                    latch.timestamp_us,
                                    latch.timestamp_us)) {
        runtime->accel_conditions |= IMU_STATUS_ACCEL_CORRECTION_INVALID;
    }
    (void)service_gyro_expiry(runtime, monotonic_clock_stm32_now_us());
    publish_runtime_snapshot(runtime);
}

static void complete_calibration(imu_runtime_t *runtime)
{
    mahony_init_from_gravity(&runtime->estimator,
                             runtime->calibration.gravity_g, 0.2f, 0.0f);
    log_calibration_complete_once(runtime);
    transition_state(runtime, IMU_RUNNING);
    update_attitude_fields(runtime);
}

static void set_accel_correction_condition(imu_runtime_t *runtime, bool valid)
{
    runtime->accel_conditions &=
        (uint16_t)~IMU_STATUS_ACCEL_CORRECTION_INVALID;
    if (!valid) {
        runtime->accel_conditions |= IMU_STATUS_ACCEL_CORRECTION_INVALID;
    }
}

static void process_calibration_gyro(imu_runtime_t *runtime,
                                     const bmi088_drdy_latch_t *latch,
                                     imu_vec3f_t gyro_dps,
                                     bool had_baseline,
                                     imu_dt_action_t action,
                                     bool overrun)
{
    const bmi088_drdy_latch_t newest_latch = bmi088_stm32_accel_latch();
    const bool accel_valid =
        runtime->newest_accel_valid &&
        imu_accel_correction_valid(
            runtime->newest_accel_g, newest_latch.sequence,
            runtime->consumed_accel_sequence,
            runtime->newest_accel_timestamp_us, latch->timestamp_us);
    const imu_calibration_admission_t admission = imu_calibration_admission(
        had_baseline, action, overrun, runtime->newest_accel_valid,
        gyro_dps, runtime->newest_accel_g, newest_latch.sequence,
        runtime->consumed_accel_sequence,
        runtime->newest_accel_timestamp_us, latch->timestamp_us);
    imu_calibration_step_t step;

    set_accel_correction_condition(runtime, accel_valid);
    if (!imu_calibration_apply_admission(
            &runtime->calibration, admission, runtime->newest_accel_g,
            gyro_dps, &step)) {
        return;
    }
    if (step == IMU_CALIBRATION_COMPLETE) {
        runtime->snapshot.gyro_dps = imu_apply_gyro_bias(
            gyro_dps, runtime->calibration.gyro_bias_dps);
        complete_calibration(runtime);
    }
}

static void process_running_gyro(imu_runtime_t *runtime,
                                 const bmi088_drdy_latch_t *latch,
                                 imu_vec3f_t gyro_dps,
                                 bool had_baseline,
                                 imu_dt_action_t action,
                                 uint32_t delta_us)
{
    bool correction_valid = false;

    runtime->snapshot.gyro_dps = imu_apply_gyro_bias(
        gyro_dps, runtime->calibration.gyro_bias_dps);

    if (had_baseline && action == IMU_DT_INTEGRATE) {
        const bmi088_drdy_latch_t newest_latch = bmi088_stm32_accel_latch();

        correction_valid =
            runtime->newest_accel_valid &&
            imu_accel_correction_valid(
                runtime->newest_accel_g, newest_latch.sequence,
                runtime->consumed_accel_sequence,
                runtime->newest_accel_timestamp_us, latch->timestamp_us);
        set_accel_correction_condition(runtime, correction_valid);
    }

    const imu_running_gyro_result_t result = imu_apply_running_gyro_timing(
        &runtime->estimator,
        had_baseline,
        action,
        scale_vec(runtime->snapshot.gyro_dps, DEG_TO_RAD),
        runtime->newest_accel_g, correction_valid,
        (float)delta_us * 0.000001f);
    if (result.rejected_dt) {
        increment_saturating(&runtime->snapshot.diagnostics.rejected_dt);
    }
    if (result.long_gap) {
        increment_saturating(&runtime->snapshot.diagnostics.long_gaps);
    }
}

static void process_gyro(imu_runtime_t *runtime)
{
    const bmi088_drdy_latch_t latch = bmi088_stm32_gyro_latch();
    const bool newer_sample =
        latch.sequence != runtime->consumed_gyro_sequence;
    const bool had_baseline = runtime->gyro_baseline_valid;
    const uint32_t delta_us =
        (uint32_t)(latch.timestamp_us - runtime->gyro_baseline_us);
    const imu_dt_action_t action = imu_classify_gyro_delta_us(delta_us);
    bmi088_raw_sample_t raw;
    imu_vec3f_t gyro_dps;
    bool overrun;

    runtime->gyro_conditions =
        imu_gyro_timestamp_condition(had_baseline, action);

    overrun = record_overrun(latch.sequence, runtime->consumed_gyro_sequence,
                             &runtime->snapshot.diagnostics.gyro_overruns);
    if (overrun) {
        runtime->gyro_conditions |= IMU_STATUS_EVENT_OVERRUN;
    }

    runtime->gyro_baseline_us = latch.timestamp_us;
    runtime->gyro_baseline_valid = true;
    const bmi088_result_t result =
        bmi088_read_gyro(&runtime->sensor, &raw, &gyro_dps);
    runtime->consumed_gyro_sequence = latch.sequence;
    if (result != BMI088_OK) {
        runtime->gyro_conditions |= IMU_STATUS_SPI_ERROR;
        publish_read_failure(runtime, latch.timestamp_us);
        return;
    }

    runtime->consecutive_read_failures = 0u;
    if (newer_sample) {
        imu_accept_new_gyro_sample(
            latch.timestamp_us, &runtime->last_gyro_timestamp_us,
            &runtime->last_gyro_timestamp_valid, &runtime->gyro_expired);
    }
    runtime->snapshot.timestamp_us = latch.timestamp_us;
    runtime->snapshot.gyro_dps = gyro_dps;
    increment_saturating(&runtime->snapshot.diagnostics.gyro_samples);
    if (imu_gyro_saturated(raw)) {
        runtime->gyro_conditions |= IMU_STATUS_GYRO_SATURATED;
    }

    if (runtime->state == IMU_CALIBRATING) {
        process_calibration_gyro(runtime, &latch, gyro_dps, had_baseline,
                                 action, overrun);
    } else {
        process_running_gyro(runtime, &latch, gyro_dps, had_baseline, action,
                             delta_us);
    }

    publish_runtime_snapshot(runtime);
}

static void wait_fault_retry(imu_runtime_t *runtime)
{
    const rt_tick_t retry_deadline =
        runtime->state_entered_tick + RT_TICK_PER_SECOND;

    for (;;) {
        const rt_tick_t now = rt_tick_get();
        rt_tick_t delay;

        update_state_led(runtime, now);
        log_diagnostics_if_due(runtime, now);
        if (tick_reached(now, retry_deadline)) {
            break;
        }

        delay = runtime->led_deadline - now;
        if ((rt_tick_t)(retry_deadline - now) < delay) {
            delay = retry_deadline - now;
        }
        if (delay == 0u) {
            continue;
        }
        rt_thread_delay(delay);
    }

    increment_saturating(
        &runtime->snapshot.diagnostics.sensor_reinitializations);
    transition_state(runtime, IMU_INITIALIZING);
}

static void imu_thread_entry(void *parameter)
{
    imu_runtime_t runtime = {
        .estimator = {.q = {1.0f, 0.0f, 0.0f, 0.0f}},
        .snapshot = {
            .quaternion = {1.0f, 0.0f, 0.0f, 0.0f},
        },
        .state = IMU_INITIALIZING,
        .gyro_conditions = IMU_STATUS_TIMESTAMP_INVALID,
    };
    (void)parameter;

    publish_runtime_snapshot(&runtime);
    log_initial_state(&runtime);
    for (;;) {
        uint32_t received = 0u;

        service_housekeeping(&runtime);
        if (runtime.state == IMU_INITIALIZING) {
            initialize_sensor(&runtime);
            continue;
        }
        if (runtime.state == IMU_FAULT_RETRY) {
            wait_fault_retry(&runtime);
            continue;
        }
        if (rt_event_recv(&imu_event, IMU_EVENT_ACCEL | IMU_EVENT_GYRO,
                          RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                          housekeeping_wait(&runtime), &received) != RT_EOK) {
            continue;
        }
        if ((received & IMU_EVENT_GYRO) != 0u) {
            process_gyro(&runtime);
        }
        if (runtime.state != IMU_FAULT_RETRY &&
            (received & IMU_EVENT_ACCEL) != 0u) {
            process_accel(&runtime);
        }
    }
}

bool imu_service_init(void)
{
    rt_err_t result;

    if (service_started) {
        return true;
    }

    snapshots[0] = (imu_snapshot_t){0};
    snapshots[1] = (imu_snapshot_t){0};
    active_snapshot = 0u;
    telemetry_drop_count = 0u;

    if (!monotonic_clock_stm32_init()) {
        return false;
    }
    if (!telemetry_uart_stm32_init()) {
        monotonic_clock_stm32_deinit();
        return false;
    }
    result = rt_event_init(&imu_event, "imu_evt", RT_IPC_FLAG_FIFO);
    if (result != RT_EOK) {
        telemetry_uart_stm32_deinit();
        monotonic_clock_stm32_deinit();
        return false;
    }
    result = rt_thread_init(&imu_thread, "imu", imu_thread_entry, NULL,
                            imu_stack, IMU_THREAD_STACK_SIZE,
                            IMU_THREAD_PRIORITY, 10u);
    if (result != RT_EOK) {
        (void)rt_event_detach(&imu_event);
        telemetry_uart_stm32_deinit();
        monotonic_clock_stm32_deinit();
        return false;
    }
    if (!bmi088_stm32_init(notify_data_ready, &imu_event)) {
        (void)rt_thread_detach(&imu_thread);
        (void)rt_event_detach(&imu_event);
        telemetry_uart_stm32_deinit();
        monotonic_clock_stm32_deinit();
        return false;
    }
    result = rt_thread_startup(&imu_thread);
    if (result != RT_EOK) {
        bmi088_stm32_deinit();
        (void)rt_thread_detach(&imu_thread);
        (void)rt_event_detach(&imu_event);
        telemetry_uart_stm32_deinit();
        monotonic_clock_stm32_deinit();
        return false;
    }
    service_started = true;
    return true;
}
