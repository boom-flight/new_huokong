#include "service_lifecycle.h"

#include <assert.h>

static bool service_started;
static bool thread_detached;
static bool child_cleanup_ok;
static char operations[4];
static unsigned operation_count;

static bool child_cleanup(void)
{
    operations[operation_count++] = 'c';
    return child_cleanup_ok;
}

static bool detach_thread(void)
{
    operations[operation_count++] = 'd';
    return true;
}

static bool cleanup_parent(void)
{
    operations[operation_count++] = 'p';
    return true;
}

static void reset_operations(void)
{
    operation_count = 0u;
    operations[0] = '\0';
}

int main(void)
{
    service_started = true;
    thread_detached = false;
    child_cleanup_ok = false;
    reset_operations();

    assert(!service_cleanup_child_then_parent(
        &service_started, &thread_detached, child_cleanup,
        detach_thread, cleanup_parent));
    assert(service_started);
    assert(!thread_detached);
    assert(operation_count == 1u);
    assert(operations[0] == 'c');

    child_cleanup_ok = true;
    reset_operations();
    assert(service_cleanup_child_then_parent(
        &service_started, &thread_detached, child_cleanup,
        detach_thread, cleanup_parent));
    assert(!service_started);
    assert(thread_detached);
    assert(operation_count == 3u);
    assert(operations[0] == 'c');
    assert(operations[1] == 'd');
    assert(operations[2] == 'p');

    reset_operations();
    assert(service_cleanup_child_then_parent(
        &service_started, &thread_detached, child_cleanup,
        detach_thread, cleanup_parent));
    assert(operation_count == 0u);
    return 0;
}
