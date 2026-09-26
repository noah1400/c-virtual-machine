#ifndef _SYSCALLS_H_
#define _SYSCALLS_H_

#include "vm_types.h"

// Services SYSCALL #number; returns a VM error code if the syscall faulted
int syscall_dispatch(VM *vm, uint16_t number);

#endif // _SYSCALLS_H_
