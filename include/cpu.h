#ifndef _CPU_H_
#define _CPU_H_

#include "vm_types.h"

void cpu_reset(VM *vm);
int cpu_execute_instruction(VM *vm, const Instruction *instr);

static inline uint8_t cpu_get_flag(const VM *vm, uint8_t flag) {
    return (vm->registers[R4_SR] & flag) != 0;
}

static inline void cpu_set_flag(VM *vm, uint8_t flag, int value) {
    if (value) {
        vm->registers[R4_SR] |= flag;
    } else {
        vm->registers[R4_SR] &= ~(uint32_t)flag;
    }
}

void cpu_stack_push(VM *vm, uint32_t value);
uint32_t cpu_stack_pop(VM *vm);
void cpu_enter_frame(VM *vm, uint32_t locals_size);
void cpu_leave_frame(VM *vm);
void cpu_push_all(VM *vm);
void cpu_pop_all(VM *vm);

void cpu_interrupt(VM *vm, uint8_t vector);
int cpu_exception(VM *vm);
void cpu_return_from_interrupt(VM *vm);
void cpu_request_interrupt(VM *vm, uint8_t vector);
int cpu_deliver_interrupt(VM *vm);
void cpu_enable_interrupts(VM *vm);
void cpu_disable_interrupts(VM *vm);

void cpu_push_frame(VM *vm, uint32_t site, uint32_t resume, int vector);
void cpu_pop_frames(VM *vm, int interrupt_return);
uint32_t cpu_unwind_calls(VM *vm, uint32_t count, uint32_t pc);

void cpu_dump_registers(VM *vm);
void cpu_print_location(const VM *vm, FILE *out, uint32_t address);
void cpu_print_backtrace(const VM *vm, FILE *out, const char *prefix);

#endif // _CPU_H_
