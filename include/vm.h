#ifndef _VM_H_
#define _VM_H_

#include "vm_types.h"
#include "instruction_set.h"

// VM lifecycle functions
int vm_init(VM *vm, uint32_t memory_size);
void vm_cleanup(VM *vm);

// VM execution functions
int vm_run(VM *vm);
int vm_step(VM *vm);

int vm_peek_instruction(const VM *vm, uint32_t address, Instruction *instr);

// Program loading
int vm_load_program(VM *vm, const uint8_t *program, uint32_t size);
int vm_load_program_file(VM *vm, const char *filename);

// Error handling
int vm_raise(VM *vm, int code, const char *format, ...);
void vm_clear_error(VM *vm);
const char *vm_get_error_string(int error_code);
const char *vm_get_error_message(const VM *vm);

#endif // _VM_H_
