#ifndef _CPU_H_
#define _CPU_H_

#include "vm_types.h"

// CPU initialization and reset
int cpu_init(VM *vm);
int cpu_reset(VM *vm);

// Instruction execution
int cpu_execute_instruction(VM *vm, Instruction *instr);
int cpu_step(VM *vm);

// Register operations
uint32_t cpu_get_register(VM *vm, uint8_t reg);
void cpu_set_register(VM *vm, uint8_t reg, uint32_t value);

// Flag operations
uint8_t cpu_get_flag(VM *vm, uint8_t flag);
void cpu_set_flag(VM *vm, uint8_t flag, uint8_t value);
void cpu_update_flags(VM *vm, uint32_t result, uint8_t flags_to_update);

// Stack operations
void cpu_stack_push(VM *vm, uint32_t value);
uint32_t cpu_stack_pop(VM *vm);
void cpu_enter_frame(VM *vm, uint16_t locals_size);
void cpu_leave_frame(VM *vm);

// Interrupt handling
void cpu_interrupt(VM *vm, uint8_t vector);
void cpu_return_from_interrupt(VM *vm);
void cpu_enable_interrupts(VM *vm);
void cpu_disable_interrupts(VM *vm);

// Debugging
void cpu_dump_registers(VM *vm);

#endif // _CPU_H_