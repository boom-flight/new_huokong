#ifndef TELEMETRY_SERVICE_H
#define TELEMETRY_SERVICE_H

#include <stdbool.h>

#define TELEMETRY_THREAD_PRIORITY 15u
#define TELEMETRY_THREAD_STACK_SIZE 512u

bool telemetry_service_init(void);

#endif
