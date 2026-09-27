#ifndef _MONITOR_H_
#define _MONITOR_H_

#include "vm_types.h"

// An executed instruction and the registers it left behind
typedef struct {
    uint32_t pc;
    uint32_t registers[16];
    uint16_t changed;       // one bit for every register the instruction changed, PC aside
    uint8_t exception;      // the vector of an exception the instruction raised, or 0
} HistoryEntry;

// What vm watches while it runs a program outside the debugger
typedef struct {
    int trace;              // print each instruction on stderr before it executes
    uint32_t *counts;       // executions of each instruction of the code, or NULL
    HistoryEntry *history;  // the last history_size instructions, or NULL
    uint32_t history_size;
    uint64_t history_count;
} Monitor;

int monitor_run(VM *vm, Monitor *monitor);
void monitor_report_profile(const VM *vm, const Monitor *monitor);
void monitor_report_history(const VM *vm, const Monitor *monitor);

#endif // _MONITOR_H_
