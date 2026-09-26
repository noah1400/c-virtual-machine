#ifndef _CPU_H_
#define _CPU_H_

#include "vm_types.h"

void cpu_reset(VM *vm);
int cpu_execute_instruction(VM *vm, const Instruction *instr);

uint8_t cpu_get_flag(const VM *vm, uint8_t flag);
void cpu_set_flag(VM *vm, uint8_t flag, int value);

void cpu_stack_push(VM *vm, uint32_t value);
uint32_t cpu_stack_pop(VM *vm);
void cpu_enter_frame(VM *vm, uint16_t locals_size);
void cpu_leave_frame(VM *vm);
void cpu_push_all(VM *vm);
void cpu_pop_all(VM *vm, int restore_pc);

void cpu_interrupt(VM *vm, uint8_t vector);
void cpu_request_interrupt(VM *vm, uint8_t vector);
int cpu_deliver_interrupt(VM *vm);
void cpu_enable_interrupts(VM *vm);
void cpu_disable_interrupts(VM *vm);

void cpu_dump_registers(VM *vm);

#endif // _CPU_H_
