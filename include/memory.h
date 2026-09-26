#ifndef _MEMORY_H_
#define _MEMORY_H_

#include "vm_types.h"

// Memory protection constants
#define PROT_NONE  0x00     // No access permissions
#define PROT_READ  0x01     // Read permission
#define PROT_WRITE 0x02     // Write permission
#define PROT_EXEC  0x04     // Execute permission
#define PROT_ALL   (PROT_READ | PROT_WRITE | PROT_EXEC)

// Internal memory management functions
int memory_init(VM *vm, uint32_t size);
void memory_cleanup(VM *vm);

// Memory access functions with bounds checking
int memory_check_address(VM *vm, uint32_t address, uint32_t size);
int memory_check_address_permissions(VM *vm, uint32_t address, uint32_t size, uint8_t required_perm);

// Low-level memory operations
uint8_t memory_read_byte(VM *vm, uint32_t address);
void memory_write_byte(VM *vm, uint32_t address, uint8_t value);
uint16_t memory_read_word(VM *vm, uint32_t address);
void memory_write_word(VM *vm, uint32_t address, uint16_t value);
uint32_t memory_read_dword(VM *vm, uint32_t address);
void memory_write_dword(VM *vm, uint32_t address, uint32_t value);

// Memory block operations
int memory_copy(VM *vm, uint32_t dest, uint32_t src, uint32_t size);
int memory_set(VM *vm, uint32_t address, uint8_t value, uint32_t size);

// Heap memory management
uint32_t memory_allocate(VM *vm, uint32_t size);
int memory_free(VM *vm, uint32_t address);
int memory_protect(VM *vm, uint32_t address, uint8_t flags);
void memory_heap_stats(const VM *vm, uint32_t *free_bytes, uint32_t *largest_free);

#endif // _MEMORY_H_
