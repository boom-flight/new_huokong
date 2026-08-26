#include "fake_rtthread_log.h"

#include <string.h>
#include <stdarg.h>

static struct rt_messagequeue *active_queue;
static rt_err_t queue_init_result;
static rt_err_t thread_init_result;
static rt_err_t thread_startup_result;
static unsigned queue_detach_count;
static unsigned defunct_execute_count;
static struct rt_thread *started_thread;
static int run_worker_on_delay;
static struct rt_thread *pending_thread;
static rt_tick_t fake_tick;

void fake_rtthread_log_reset(void)
{
    thread_init_result = RT_EOK;
    thread_startup_result = RT_EOK;
    queue_init_result = RT_EOK;
    active_queue = NULL;
    queue_detach_count = 0u;
    defunct_execute_count = 0u;
    started_thread = NULL;
    run_worker_on_delay = 0;
    pending_thread = NULL;
    fake_tick = 0u;
}

void fake_rtthread_log_set_thread_init_result(rt_err_t result)
{
    thread_init_result = result;
}

void fake_rtthread_log_set_thread_startup_result(rt_err_t result)
{
    thread_startup_result = result;
}

void fake_rtthread_log_set_queue_init_result(rt_err_t result)
{
    queue_init_result = result;
}

void fake_rtthread_log_set_run_worker_on_delay(int enabled)
{
    run_worker_on_delay = enabled;
}

rt_err_t rt_mq_init(struct rt_messagequeue *mq, const char *name,
                    void *msgpool, rt_size_t msg_size, rt_size_t pool_size,
                    rt_uint8_t flag)
{
    (void)name;
    (void)flag;
    (void)msgpool;
    if (queue_init_result != RT_EOK) {
        return queue_init_result;
    }
    active_queue = mq;
    mq->message_size = msg_size;
    mq->capacity = pool_size / msg_size;
    mq->count = 0u;
    return RT_EOK;
}

rt_err_t rt_mq_detach(struct rt_messagequeue *mq)
{
    if (active_queue == mq) {
        active_queue = NULL;
        ++queue_detach_count;
    }
    return RT_EOK;
}

rt_err_t rt_mq_send(struct rt_messagequeue *mq, const void *buffer,
                    rt_size_t size)
{
    if (size != mq->message_size || mq->count == mq->capacity ||
        mq->count * mq->message_size + size > sizeof mq->storage) {
        return -RT_EFULL;
    }
    memcpy(&mq->storage[mq->count * mq->message_size], buffer, size);
    ++mq->count;
    return RT_EOK;
}

rt_err_t rt_mq_recv(struct rt_messagequeue *mq, void *buffer, rt_size_t size,
                    rt_int32_t timeout)
{
    (void)timeout;
    if (size != mq->message_size || mq->count == 0u) {
        return -RT_EEMPTY;
    }
    memcpy(buffer, mq->storage, size);
    memmove(mq->storage, &mq->storage[mq->message_size],
            (mq->count - 1u) * mq->message_size);
    --mq->count;
    return RT_EOK;
}

int fake_rtthread_log_receive(void *buffer, rt_size_t size)
{
    return active_queue != NULL &&
                   rt_mq_recv(active_queue, buffer, size, 0) == RT_EOK
               ? 1
               : 0;
}

int fake_rtthread_log_queue_active(void)
{
    return active_queue != NULL;
}

unsigned fake_rtthread_log_queue_detach_count(void)
{
    return queue_detach_count;
}

unsigned fake_rtthread_log_defunct_execute_count(void)
{
    return defunct_execute_count;
}

int fake_rtthread_log_deferred_cleanup_pending(void)
{
    return pending_thread != NULL;
}

rt_err_t rt_thread_init(struct rt_thread *thread, const char *name,
                        void (*entry)(void *parameter), void *parameter,
                        void *stack_start, rt_uint32_t stack_size,
                        rt_uint8_t priority, rt_uint32_t tick)
{
    (void)name;
    (void)stack_start;
    (void)stack_size;
    (void)priority;
    (void)tick;
    if (thread->state == FAKE_RT_THREAD_DEFUNCT ||
        pending_thread == thread) {
        return -1;
    }
    if (thread_init_result != RT_EOK) {
        return thread_init_result;
    }
    thread->entry = entry;
    thread->parameter = parameter;
    thread->state = FAKE_RT_THREAD_INIT;
    return thread_init_result;
}

rt_err_t rt_thread_startup(struct rt_thread *thread)
{
    if (thread_startup_result == RT_EOK) {
        started_thread = thread;
        thread->state = FAKE_RT_THREAD_RUNNING;
    }
    return thread_startup_result;
}

rt_err_t rt_thread_detach(struct rt_thread *thread)
{
    thread->state = FAKE_RT_THREAD_DEFUNCT;
    pending_thread = thread;
    if (started_thread == thread) {
        started_thread = NULL;
    }
    return RT_EOK;
}

rt_err_t rt_thread_suspend(struct rt_thread *thread)
{
    thread->state = FAKE_RT_THREAD_SUSPENDED;
    return RT_EOK;
}

static void fake_thread_wait(void)
{
    ++fake_tick;
    if (run_worker_on_delay && started_thread != NULL) {
        struct rt_thread *thread = started_thread;

        started_thread = NULL;
        thread->entry(thread->parameter);
    }
}

rt_err_t rt_thread_delay(rt_tick_t ticks)
{
    while (ticks != 0u) {
        fake_thread_wait();
        --ticks;
    }
    return RT_EOK;
}

rt_err_t rt_thread_mdelay(rt_int32_t ms)
{
    (void)ms;
    fake_thread_wait();
    return RT_EOK;
}

rt_tick_t rt_tick_get(void)
{
    return fake_tick;
}

void rt_defunct_execute(void)
{
    ++defunct_execute_count;
    if (pending_thread != NULL) {
        pending_thread->state = FAKE_RT_THREAD_DETACHED;
        pending_thread = NULL;
    }
}

int rt_kprintf(const char *format, ...)
{
    (void)format;
    return 0;
}
