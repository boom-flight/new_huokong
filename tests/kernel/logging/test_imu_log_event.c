#include "logging/imu_log_event.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

static void assert_format(imu_log_event_t event, const char *expected)
{
    char buffer[192];
    const size_t expected_length = strlen(expected);

    memset(buffer, 0xA5, sizeof buffer);
    assert(imu_log_event_format(&event, buffer, sizeof buffer) ==
           expected_length);
    assert(strcmp(buffer, expected) == 0);
}

static void test_formats_basic_events_with_existing_log_strings(void)
{
    assert_format((imu_log_event_t){
                       .kind = IMU_LOG_INITIAL_STATE,
                       .state = IMU_INITIALIZING,
                   },
                   "IMU state: initializing\n");
    assert_format((imu_log_event_t){
                       .kind = IMU_LOG_STATE,
                       .previous_state = IMU_CALIBRATING,
                       .state = IMU_RUNNING,
                   },
                   "IMU state: calibrating -> running\n");
    assert_format((imu_log_event_t){
                       .kind = IMU_LOG_IDS,
                       .accel_id = 0x1e,
                       .gyro_id = 0x0f,
                   },
                   "BMI088 IDs: accel=0x1e gyro=0x0f\n");
    assert_format((imu_log_event_t){
                       .kind = IMU_LOG_CALIBRATION_COMPLETE,
                   },
                   "IMU calibration complete\n");
}

static void test_formats_all_diagnostic_counters(void)
{
    assert_format((imu_log_event_t){
                       .kind = IMU_LOG_DIAGNOSTICS,
                       .diagnostics = {
                           .accel_samples = 1u,
                           .gyro_samples = 2u,
                           .accel_overruns = 3u,
                           .gyro_overruns = 4u,
                           .spi_errors = 5u,
                           .rejected_dt = 6u,
                           .long_gaps = 7u,
                           .sensor_reinitializations = 8u,
                           .telemetry_drops = 9u,
                       },
                   },
                   "IMU errors: spi=5 accel_overrun=3 gyro_overrun=4 "
                   "rejected_dt=6 long_gap=7 telemetry_drop=9 reinit=8\n");
}

static void test_formats_maximum_diagnostic_counters(void)
{
    const char *expected =
        "IMU errors: spi=4294967295 accel_overrun=4294967295 "
        "gyro_overrun=4294967295 rejected_dt=4294967295 "
        "long_gap=4294967295 telemetry_drop=4294967295 "
        "reinit=4294967295\n";
    imu_log_event_t event = {
        .kind = IMU_LOG_DIAGNOSTICS,
        .diagnostics = {
            .spi_errors = UINT32_MAX,
            .accel_overruns = UINT32_MAX,
            .gyro_overruns = UINT32_MAX,
            .rejected_dt = UINT32_MAX,
            .long_gaps = UINT32_MAX,
            .sensor_reinitializations = UINT32_MAX,
            .telemetry_drops = UINT32_MAX,
        },
    };

    assert_format(event, expected);
}

static void test_bounded_format_returns_required_length_and_terminates(void)
{
    const imu_log_event_t event = {
        .kind = IMU_LOG_INITIAL_STATE,
        .state = IMU_INITIALIZING,
    };
    char buffer[8];
    const char *expected = "IMU state: initializing\n";

    memset(buffer, 0xA5, sizeof buffer);
    assert(imu_log_event_format(&event, buffer, sizeof buffer) ==
           strlen(expected));
    assert(buffer[sizeof buffer - 1u] == '\0');
    assert(strncmp(buffer, expected, sizeof buffer - 1u) == 0);
}

static void test_rejects_invalid_arguments_and_event_kind(void)
{
    const imu_log_event_t event = {
        .kind = IMU_LOG_CALIBRATION_COMPLETE,
    };
    char buffer[32] = {0};
    const imu_log_event_t invalid = {.kind = (imu_log_event_kind_t)99};

    assert(imu_log_event_format(NULL, buffer, sizeof buffer) == 0u);
    assert(imu_log_event_format(&event, NULL, sizeof buffer) == 0u);
    assert(imu_log_event_format(&event, buffer, 0u) == 0u);
    assert(imu_log_event_format(&invalid, buffer, sizeof buffer) == 0u);
    assert(buffer[0] == '\0');
}

int main(void)
{
    test_formats_basic_events_with_existing_log_strings();
    test_formats_all_diagnostic_counters();
    test_formats_maximum_diagnostic_counters();
    test_bounded_format_returns_required_length_and_terminates();
    test_rejects_invalid_arguments_and_event_kind();
    return 0;
}
