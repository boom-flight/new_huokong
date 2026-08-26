/**
 * @file dma_tx_state.h
 * @brief DMA 发送占用、释放和异步失败状态管理接口。
 */

#ifndef DMA_TX_STATE_H
#define DMA_TX_STATE_H

#include <stdbool.h>

/**
 * @brief DMA 发送状态。
 */
typedef struct {
    /** @brief 是否已有发送操作占用 DMA。 */
    bool busy;
    /** @brief 是否有尚未被发送方取走的异步失败事件。 */
    bool failure_pending;
} dma_tx_state_t;

/**
 * @brief 将 DMA 发送状态恢复为空闲且无待处理失败。
 * @param state 待重置的状态。
 */
void dma_tx_state_reset(dma_tx_state_t *state);

/**
 * @brief 尝试占用 DMA 发送通道。
 * @param state DMA 发送状态。
 * @return 通道原本空闲并成功占用时返回 true，否则返回 false。
 */
bool dma_tx_state_reserve(dma_tx_state_t *state);

/**
 * @brief 释放一次已占用的 DMA 发送。
 * @param state DMA 发送状态。
 */
void dma_tx_state_release(dma_tx_state_t *state);

/**
 * @brief 记录一次正在进行的 DMA 发送发生异步错误。
 * @param state DMA 发送状态。
 * @note 仅当发送处于 busy 状态时记录失败，并同时释放占用。
 */
void dma_tx_state_async_error(dma_tx_state_t *state);

/**
 * @brief 查询 DMA 发送通道是否被占用。
 * @param state DMA 发送状态。
 * @return 当前处于 busy 状态时返回 true。
 */
bool dma_tx_state_busy(const dma_tx_state_t *state);

/**
 * @brief 取出并清除一次待处理的异步失败事件。
 * @param state DMA 发送状态。
 * @return 存在待处理失败事件时返回 true。
 */
bool dma_tx_state_take_failure(dma_tx_state_t *state);

#endif
