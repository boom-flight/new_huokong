/**
 * @file timestamp_extender.h
 * @brief 将 16 位回绕计数器扩展为 32 位时间戳的接口。
 */

#ifndef TIMESTAMP_EXTENDER_H
#define TIMESTAMP_EXTENDER_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 根据回绕更新状态合成 32 位时间戳。
 * @param high_word 当前保存的高 16 位。
 * @param counter 当前读取的低 16 位计数器。
 * @param update_pending 是否存在尚未并入高位的回绕更新。
 * @return 合成后的 32 位时间戳。
 * @note 仅当计数器位于回绕后的低半区时应用待处理的高位递增，避免在临界区间重复进位。
 */
uint32_t timestamp_extender_compose(uint16_t high_word, uint16_t counter,
                                    bool update_pending);

#endif
