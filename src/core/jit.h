#ifndef _JIT_H_
#define _JIT_H_

#include "run.h"

// Where cpu_run goes on after compiled code: with the word it names, with that word but not with its compiled
// code, or at the address it names, which lies where cpu_run does not run
enum { JIT_NEXT, JIT_HERE, JIT_LEAVE };

// Compiles the instructions from a word below the words cpu_run runs; tells whether that word now starts
// compiled code, which its kind D_JIT announces
int jit_compile(VM *vm, uint32_t index);

// Runs the compiled code from a word while left, the instructions cpu_run may still run, allows; updates left
// and the flags and returns where cpu_run goes on, as how says
uint32_t jit_run(VM *vm, uint32_t index, uint32_t *left, Flags *flags, uint32_t *how);

// The kind of a word before compiled code took it over
uint8_t jit_kind(const VM *vm, uint32_t index);

// Drops the compiled code if size bytes written at an address change instructions it came from
void jit_written(VM *vm, uint32_t address, uint32_t size);

void jit_free(VM *vm);

#endif // _JIT_H_
