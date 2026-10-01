#include "jit.h"

// No code gets compiled yet: cpu_run runs every instruction itself

int jit_compile(VM *vm, uint32_t index) {
    (void)vm;
    (void)index;
    return 0;
}

uint32_t jit_run(VM *vm, uint32_t index, uint32_t *left, Flags *flags, uint32_t *how) {
    (void)vm;
    (void)left;
    (void)flags;
    *how = JIT_HERE;
    return index;
}

uint8_t jit_kind(const VM *vm, uint32_t index) {
    (void)vm;
    (void)index;
    return D_STOP;
}

void jit_written(VM *vm, uint32_t address, uint32_t size) {
    (void)vm;
    (void)address;
    (void)size;
}

void jit_free(VM *vm) {
    (void)vm;
}
