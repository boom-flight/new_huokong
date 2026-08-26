/**
 * @file service_lifecycle.h
 * @brief 服务线程停止等待和分层资源清理辅助函数。
 */

#ifndef SERVICE_LIFECYCLE_H
#define SERVICE_LIFECYCLE_H

#include <rtthread.h>

#include <stdbool.h>

/** @brief 服务线程停止等待的最长时长，单位为 RT-Thread tick。 */
enum {
    SERVICE_THREAD_STOP_TIMEOUT_TICKS = RT_TICK_PER_SECOND,
};

/** @brief 无参数服务清理回调。 */
typedef bool (*service_cleanup_callback_t)(void);

/**
 * @brief 按子服务、线程、父资源的顺序执行服务清理。
 *
 * @param[in,out] service_started 服务是否仍被标记为已启动。
 * @param[in,out] thread_detached 线程是否已脱离内核对象管理。
 * @param child_cleanup 子服务清理回调。
 * @param detach_thread 线程脱离回调。
 * @param parent_cleanup 父服务资源清理回调。
 * @return 所有需要的清理步骤成功时为 true；任一步骤失败时为 false。
 * @note 回调失败时保留相应状态标记，便于后续继续清理。
 */
static inline bool service_cleanup_child_then_parent(
    bool *service_started,
    bool *thread_detached,
    service_cleanup_callback_t child_cleanup,
    service_cleanup_callback_t detach_thread,
    service_cleanup_callback_t parent_cleanup)
{
    if (!*service_started) {
        return true;
    }
    if (!child_cleanup()) {
        return false;
    }
    if (!*thread_detached) {
        if (!detach_thread()) {
            return false;
        }
        *thread_detached = true;
    }
    if (!parent_cleanup()) {
        return false;
    }
    *service_started = false;
    return true;
}

/**
 * @brief 在有限等待时间内等待服务线程报告已停止。
 *
 * @param stopped 指向线程停止标志的指针。
 * @return 线程在超时前已停止时为 true，否则为 false。
 */
static inline bool service_wait_for_thread_stop(
    const volatile bool *stopped)
{
    rt_tick_t waited;

    for (waited = 0u;
         waited < SERVICE_THREAD_STOP_TIMEOUT_TICKS && !*stopped;
         ++waited) {
        rt_thread_delay(1u);
    }
    return *stopped;
}

#endif
