/**
 * @file imu_log_event.c
 * @brief IMU 日志事件的状态名称和无动态分配文本格式化实现。
 */

#include "logging/imu_log_event.h"

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
} imu_log_writer_t;

/**
 * @brief 将 IMU 状态枚举转换为日志中的稳定文本名称。
 *
 * @param state IMU 状态。
 * @return 对应的状态名称；未知枚举值返回 "unknown"。
 */
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

/**
 * @brief 向格式化写入器追加一个字符，并累计完整输出长度。
 *
 * @param writer 格式化写入器。
 * @param value 要追加的字符。
 * @note 缓冲区不足时不写入字符，但仍累计完整输出长度以便报告截断。
 */
static void write_char(imu_log_writer_t *writer, char value)
{
    if (writer->length + 1u < writer->capacity) {
        writer->buffer[writer->length] = value;
    }
    ++writer->length;
}

/**
 * @brief 向格式化写入器追加一个以空字符结尾的字符串。
 *
 * @param writer 格式化写入器。
 * @param text 要追加的文本。
 */
static void write_text(imu_log_writer_t *writer, const char *text)
{
    while (*text != '\0') {
        write_char(writer, *text++);
    }
}

/**
 * @brief 将无符号整数转换为十进制并追加到格式化写入器。
 *
 * @param writer 格式化写入器。
 * @param value 要写入的数值。
 */
static void write_uint(imu_log_writer_t *writer, uint32_t value)
{
    char digits[10];
    size_t count = 0u;

    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);

    while (count != 0u) {
        write_char(writer, digits[--count]);
    }
}

/**
 * @brief 将一个字节以两位小写十六进制形式追加到写入器。
 *
 * @param writer 格式化写入器。
 * @param value 要写入的字节。
 */
static void write_hex_byte(imu_log_writer_t *writer, uint8_t value)
{
    static const char digits[] = "0123456789abcdef";

    write_char(writer, digits[(value >> 4u) & 0x0fu]);
    write_char(writer, digits[value & 0x0fu]);
}

/**
 * @brief 为格式化输出添加结尾空字符，并在必要时截断输出。
 *
 * @param writer 格式化写入器。
 */
static void finish(imu_log_writer_t *writer)
{
    size_t index = writer->length < writer->capacity
                       ? writer->length
                       : writer->capacity - 1u;
    writer->buffer[index] = '\0';
}

/**
 * @brief 将日志事件编码为可直接输出的文本。
 *
 * @param event 要格式化的事件。
 * @param[out] buffer 接收文本的缓冲区。
 * @param capacity 缓冲区容量。
 * @return 完整文本长度；参数无效或事件类型未知时返回 0。
 */
size_t imu_log_event_format(const imu_log_event_t *event, char *buffer,
                            size_t capacity)
{
    imu_log_writer_t writer;

    if (event == NULL || buffer == NULL || capacity == 0u) {
        return 0u;
    }

    writer = (imu_log_writer_t){
        .buffer = buffer,
        .capacity = capacity,
        .length = 0u,
    };
    switch (event->kind) {
    case IMU_LOG_INITIAL_STATE:
        write_text(&writer, "IMU state: ");
        write_text(&writer, state_name(event->state));
        write_char(&writer, '\n');
        break;
    case IMU_LOG_STATE:
        write_text(&writer, "IMU state: ");
        write_text(&writer, state_name(event->previous_state));
        write_text(&writer, " -> ");
        write_text(&writer, state_name(event->state));
        write_char(&writer, '\n');
        break;
    case IMU_LOG_IDS:
        write_text(&writer, "BMI088 IDs: accel=0x");
        write_hex_byte(&writer, event->accel_id);
        write_text(&writer, " gyro=0x");
        write_hex_byte(&writer, event->gyro_id);
        write_char(&writer, '\n');
        break;
    case IMU_LOG_CALIBRATION_COMPLETE:
        write_text(&writer, "IMU calibration complete\n");
        break;
    case IMU_LOG_DIAGNOSTICS:
        write_text(&writer, "IMU errors: spi=");
        write_uint(&writer, event->diagnostics.spi_errors);
        write_text(&writer, " accel_overrun=");
        write_uint(&writer, event->diagnostics.accel_overruns);
        write_text(&writer, " gyro_overrun=");
        write_uint(&writer, event->diagnostics.gyro_overruns);
        write_text(&writer, " rejected_dt=");
        write_uint(&writer, event->diagnostics.rejected_dt);
        write_text(&writer, " long_gap=");
        write_uint(&writer, event->diagnostics.long_gaps);
        write_text(&writer, " telemetry_drop=");
        write_uint(&writer, event->diagnostics.telemetry_drops);
        write_text(&writer, " reinit=");
        write_uint(&writer, event->diagnostics.sensor_reinitializations);
        write_char(&writer, '\n');
        break;
    default:
        buffer[0] = '\0';
        return 0u;
    }

    finish(&writer);
    return writer.length;
}
