#ifndef _VM_H_
#define _VM_H_

#include "vm_types.h"
#include "instruction_set.h"

// VM lifecycle functions
int vm_init(VM *vm, uint32_t memory_size);
void vm_cleanup(VM *vm);
int vm_reset(VM *vm);

// VM execution functions
int vm_run(VM *vm);                       // Run until halted
int vm_step(VM *vm);                      // Execute single instruction
int vm_execute_instruction(VM *vm);       // Execute current instruction at PC

// Memory operations
uint8_t vm_read_byte(VM *vm, uint16_t address);
void vm_write_byte(VM *vm, uint16_t address, uint8_t value);
uint16_t vm_read_word(VM *vm, uint16_t address);
void vm_write_word(VM *vm, uint16_t address, uint16_t value);
uint32_t vm_read_dword(VM *vm, uint16_t address);
void vm_write_dword(VM *vm, uint16_t address, uint32_t value);

// Instruction operations
int vm_decode_instruction(VM *vm, uint16_t address, Instruction *instr);
uint32_t vm_fetch_instruction(VM *vm);

// Stack operations
void vm_stack_push(VM *vm, uint32_t value);
uint32_t vm_stack_pop(VM *vm);
void vm_create_stack_frame(VM *vm, uint16_t params_size, uint16_t locals_size);
void vm_destroy_stack_frame(VM *vm);

// I/O operations
int vm_io_read(VM *vm, uint16_t port);
void vm_io_write(VM *vm, uint16_t port, uint32_t value);

// Interrupt handling
void vm_interrupt(VM *vm, uint8_t vector);
void vm_set_interrupt_handler(VM *vm, uint8_t vector, uint16_t handler_address);

// Debug functions
void vm_dump_state(VM *vm);
void vm_set_breakpoint(VM *vm, uint16_t address);
void vm_clear_breakpoint(VM *vm, uint16_t address);

// Program loading
int vm_load_program(VM *vm, const uint8_t *program, uint32_t size);
int vm_load_program_file(VM *vm, const char *filename);

// Error handling
const char* vm_get_error_string(int error_code);
int vm_get_last_error(VM *vm);
const char* vm_get_last_error_message(VM *vm);
const char* vm_get_error_message(VM *vm);

#endif // _VM_H_