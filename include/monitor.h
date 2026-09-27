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

#define LOG_ITEMS 8

// A value that a logpoint prints: a register, or memory at a register plus an offset
typedef struct {
    char text[40];          // the item as written
    int memory;
    int reg;                // the register, or the base of the address; -1 for none
    uint32_t offset;
    uint32_t size;          // bytes read from memory
    char format;            // x, d, u, c, s or f
} LogItem;

typedef struct {
    uint32_t address;
    LogItem items[LOG_ITEMS];
    int item_count;
} Logpoint;

// What vm watches while it runs a program outside the debugger
typedef struct {
    int trace;              // print each instruction on stderr before it executes
    uint32_t *counts;       // executions of each instruction of the code, or NULL
    HistoryEntry *history;  // the last history_size instructions, or NULL
    uint32_t history_size;
    uint64_t history_count;
    Logpoint *logpoints;
    int logpoint_count;
} Monitor;

int monitor_run(VM *vm, Monitor *monitor);
void monitor_free(Monitor *monitor);
void monitor_report_profile(const VM *vm, const Monitor *monitor);
void monitor_report_history(const VM *vm, const Monitor *monitor);

// Adds a logpoint written as LOCATION or LOCATION:ITEM,ITEM...; returns 0, or 1 after describing the
// problem in error
int monitor_add_logpoint(Monitor *monitor, const VM *vm, const char *spec, char *error, size_t size);

#endif // _MONITOR_H_
