#ifndef TEST_FAKE_RTTHREAD_H
#define TEST_FAKE_RTTHREAD_H

#include <stddef.h>
#include <stdint.h>

typedef int32_t rt_err_t;
typedef size_t rt_size_t;
typedef uint8_t rt_uint8_t;
typedef uint32_t rt_uint32_t;
typedef int32_t rt_int32_t;
typedef uint32_t rt_tick_t;

typedef enum {
    FAKE_RT_THREAD_UNINITIALIZED,
    FAKE_RT_THREAD_INIT,
    FAKE_RT_THREAD_RUNNING,
    FAKE_RT_THREAD_SUSPENDED,
    FAKE_RT_THREAD_DEFUNCT,
    FAKE_RT_THREAD_DETACHED,
} fake_rt_thread_state_t;

struct rt_messagequeue {
    rt_size_t message_size;
    rt_size_t capacity;
    rt_size_t count;
    rt_uint8_t storage[1024];
};

struct rt_thread {
    void (*entry)(void *parameter);
    void *parameter;
    fake_rt_thread_state_t state;
};

#define RT_EOK 0
#define RT_EFULL 1
#define RT_EEMPTY 2
#define RT_WAITING_FOREVER (-1)
#define RT_TICK_PER_SECOND 1000u
#define RT_ALIGN_SIZE 8u
#define RT_IPC_FLAG_FIFO 0u
#define RT_MQ_BUF_SIZE(msg_size, max_msgs) ((msg_size) * (max_msgs))
#define rt_align(n) __attribute__((aligned(n)))

rt_err_t rt_mq_init(struct rt_messagequeue *mq, const char *name,
                    void *msgpool, rt_size_t msg_size, rt_size_t pool_size,
                    rt_uint8_t flag);
rt_err_t rt_mq_send(struct rt_messagequeue *mq, const void *buffer,
                    rt_size_t size);
rt_err_t rt_mq_detach(struct rt_messagequeue *mq);
rt_err_t rt_mq_recv(struct rt_messagequeue *mq, void *buffer, rt_size_t size,
                    rt_int32_t timeout);
rt_err_t rt_thread_init(struct rt_thread *thread, const char *name,
                        void (*entry)(void *parameter), void *parameter,
                        void *stack_start, rt_uint32_t stack_size,
                        rt_uint8_t priority, rt_uint32_t tick);
rt_err_t rt_thread_startup(struct rt_thread *thread);
rt_err_t rt_thread_detach(struct rt_thread *thread);
rt_err_t rt_thread_suspend(struct rt_thread *thread);
rt_err_t rt_thread_delay(rt_tick_t ticks);
rt_err_t rt_thread_mdelay(rt_int32_t ms);
rt_tick_t rt_tick_get(void);
void rt_defunct_execute(void);
int rt_kprintf(const char *format, ...);

#endif
