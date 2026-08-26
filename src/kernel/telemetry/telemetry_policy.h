/**
 * @file telemetry_policy.h
 * @brief 遥测发送尝试状态和丢弃策略接口。
 */

#ifndef TELEMETRY_POLICY_H
#define TELEMETRY_POLICY_H

#include <stdbool.h>
#include <stdint.h>

/** @brief 遥测发送尝试的序列号和丢弃粘滞状态。 */
typedef struct {
    /** @brief 下一次发送尝试使用的序列号。 */
    uint16_t next_sequence;
    /** @brief 最近一次发送尝试是否被丢弃。 */
    bool drop_sticky;
} telemetry_attempt_state_t;

/**
 * @brief 开始一次遥测发送尝试并分配序列号。
 *
 * @param state 要更新的发送状态。
 * @return 本次尝试使用的序列号。
 */
uint16_t telemetry_attempt_begin(telemetry_attempt_state_t *state);

/**
 * @brief 标记一次遥测发送丢弃，并保持丢弃状态直到成功排队。
 *
 * @param state 要更新的发送状态。
 */
void telemetry_attempt_dropped(telemetry_attempt_state_t *state);

/**
 * @brief 标记一次遥测帧已成功排队，并清除丢弃粘滞状态。
 *
 * @param state 要更新的发送状态。
 */
void telemetry_attempt_queued(telemetry_attempt_state_t *state);

#endif
