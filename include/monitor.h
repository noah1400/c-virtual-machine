#ifndef _MONITOR_H_
#define _MONITOR_H_

#include "vm_types.h"

// What vm watches while it runs a program outside the debugger
typedef struct {
    int trace;              // print each instruction on stderr before it executes
    uint32_t *counts;       // executions of each instruction of the code, or NULL
} Monitor;

int monitor_run(VM *vm, Monitor *monitor);
void monitor_report_profile(const VM *vm, const Monitor *monitor);

#endif // _MONITOR_H_
