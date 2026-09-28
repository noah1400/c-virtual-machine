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

// Page table entry bits
#define PAGE_PRESENT 0x1
#define PAGE_WRITE   0x2
#define PAGE_USER    0x4
#define PAGE_EXEC    0x8

// ECODE bits describing a page fault
#define FAULT_PRESENT 0x1   // the page is mapped but does not allow the access
#define FAULT_WRITE   0x2
#define FAULT_USER    0x4
#define FAULT_FETCH   0x8

// Accesses at virtual addresses: heap rules, then page tables when PTB is set, then memory bounds
int memory_check_range(VM *vm, uint32_t address, uint32_t size, uint8_t access);
int memory_read(VM *vm, uint32_t address, void *buffer, uint32_t size);
int memory_write(VM *vm, uint32_t address, const void *buffer, uint32_t size);
int memory_fetch(VM *vm, uint32_t address, uint32_t *word);
uint32_t memory_peek(const VM *vm, uint32_t address, void *buffer, uint32_t size);
uint8_t *memory_direct(VM *vm, uint32_t address, uint32_t size, uint8_t access);

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
void memory_heap_reset(VM *vm);
uint32_t memory_allocate(VM *vm, uint32_t size);
int memory_free(VM *vm, uint32_t address);
int memory_protect(VM *vm, uint32_t address, uint8_t flags);
void memory_heap_stats(const VM *vm, uint32_t *free_bytes, uint32_t *largest_free);

#endif // _MEMORY_H_
