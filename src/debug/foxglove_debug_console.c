#include "rtconfig.h"

#if defined(HUOKONG_FOXGLOVE_DEBUG)

#include <rtthread.h>

int rt_kprintf(const char *format, ...)
{
    (void)format;
    return 0;
}

#else

typedef int foxglove_debug_console_disabled_translation_unit_t;

#endif
