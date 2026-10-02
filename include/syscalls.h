#ifndef _SYSCALLS_H_
#define _SYSCALLS_H_

#include "vm_types.h"

// Arguments are passed in R0, R5, R6 and R7; results come back in R0 and a status in R5,
// where 0 means success and other values are VM error codes describing the failure
enum {
    SYS_PUTCHAR      = 0,
    SYS_PRINT_INT    = 1,
    SYS_PRINT_STRING = 2,
    SYS_GETCHAR      = 3,
    SYS_READ_LINE    = 4,
    SYS_PRINT_HEX    = 5,
    SYS_PRINT_BASE   = 6,
    SYS_PRINT_FLOAT  = 7,
    SYS_CLEAR_SCREEN = 8,
    SYS_SET_COLOR    = 9,
    SYS_OPEN         = 10,
    SYS_CLOSE        = 11,
    SYS_READ         = 12,
    SYS_WRITE        = 13,
    SYS_SEEK         = 14,
    SYS_ALLOC        = 20,
    SYS_FREE         = 21,
    SYS_MEMCPY       = 22,
    SYS_MEMINFO      = 23,
    SYS_FILL         = 24,
    SYS_COMPARE      = 25,
    SYS_FIND         = 26,
    SYS_COUNT        = 27,
    SYS_SORT         = 28,
    SYS_HASH         = 29,
    SYS_EXIT         = 30,
    SYS_SLEEP        = 31,
    SYS_TIME         = 32,
    SYS_TICKS        = 33,
    SYS_ARGUMENT     = 34,
    SYS_ABORT        = 35,
    SYS_CLOCK        = 36,
    SYS_RANDOM       = 40,
    SYS_SEED         = 41,
};

// The memory syscalls, which report problems in R5 instead of faulting, so that the fast loop and
// compiled code run them themselves in supervisor mode
static inline int syscall_runs_inline(uint32_t number) {
    return number >= SYS_ALLOC && number <= SYS_HASH;
}

void syscalls_init(VM *vm);

// The days from 1970-01-01 to a date of the Gregorian calendar
int64_t days_from_civil(int64_t year, unsigned month, unsigned day);
void syscalls_cleanup(VM *vm);

// Services SYSCALL #number; returns a VM error code if the syscall faulted
int syscall_dispatch(VM *vm, uint32_t number);

#endif // _SYSCALLS_H_
