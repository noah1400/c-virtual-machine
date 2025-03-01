#ifndef _MEMORY_H_
#define _MEMORY_H_

#include "vm_types.h"

// Internal memory management functions
int memory_init(VM *vm, uint32_t size);
void memory_cleanup(VM *vm);

// Memory access functions with bounds checking
int memory_check_address(VM *vm, uint16_t address, uint16_t size);
uint8_t* memory_get_ptr(VM *vm, uint16_t address);

// Low-level memory operations
uint8_t memory_read_byte(VM *vm, uint16_t address);
void memory_write_byte(VM *vm, uint16_t address, uint8_t value);
uint16_t memory_read_word(VM *vm, uint16_t address);
void memory_write_word(VM *vm, uint16_t address, uint16_t value);
uint32_t memory_read_dword(VM *vm, uint16_t address);
void memory_write_dword(VM *vm, uint16_t address, uint32_t value);

// Memory block operations
int memory_copy(VM *vm, uint16_t dest, uint16_t src, uint16_t size);
int memory_set(VM *vm, uint16_t address, uint8_t value, uint16_t size);

// Heap memory management
uint16_t memory_allocate(VM *vm, uint16_t size);
int memory_free(VM *vm, uint16_t address);

// String detection for debugging
int memory_might_be_string(VM *vm, uint16_t addr);
char* memory_extract_string(VM *vm, uint16_t addr, int max_length);

#endif // _MEMORY_H_