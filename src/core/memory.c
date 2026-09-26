#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory.h"
#include "vm.h"

// Heap blocks tile the heap segment. Each starts with an 8-byte header stored in
// VM memory: magic (u16), total size including header (u16), flags (u8), protection (u8).
#define HEAP_BLOCK_MAGIC  0xABCD
#define HEAP_HEADER_SIZE  8u
#define HEAP_MIN_ALLOC    8u
#define HEAP_END          (HEAP_SEGMENT_BASE + HEAP_SEGMENT_SIZE)
#define BLOCK_ALLOCATED   0x01

static uint32_t block_size(const VM *vm, uint32_t block) {
    return vm->memory[block + 2] | (vm->memory[block + 3] << 8);
}

static int block_allocated(const VM *vm, uint32_t block) {
    return vm->memory[block + 4] & BLOCK_ALLOCATED;
}

static void write_block(VM *vm, uint32_t block, uint32_t size, int allocated, uint8_t protection) {
    vm->memory[block] = HEAP_BLOCK_MAGIC & 0xFF;
    vm->memory[block + 1] = HEAP_BLOCK_MAGIC >> 8;
    vm->memory[block + 2] = size & 0xFF;
    vm->memory[block + 3] = (size >> 8) & 0xFF;
    vm->memory[block + 4] = allocated ? BLOCK_ALLOCATED : 0;
    vm->memory[block + 5] = protection;
    vm->memory[block + 6] = 0;
    vm->memory[block + 7] = 0;
}

static int block_valid(const VM *vm, uint32_t block) {
    uint32_t size = block_size(vm, block);
    return (vm->memory[block] | (vm->memory[block + 1] << 8)) == HEAP_BLOCK_MAGIC &&
           size >= HEAP_HEADER_SIZE && block + size <= HEAP_END;
}

// Returns the header address of the block whose data area contains address, or 0
static uint32_t find_block(const VM *vm, uint32_t address) {
    uint32_t block = HEAP_SEGMENT_BASE;
    while (block < HEAP_END && block_valid(vm, block)) {
        uint32_t size = block_size(vm, block);
        if (address >= block + HEAP_HEADER_SIZE && address < block + size) {
            return block;
        }
        block += size;
    }
    return 0;
}

// Initialize memory for the VM
int memory_init(VM *vm, uint32_t size) {
    if (!vm) {
        return VM_ERROR_INVALID_ADDRESS;
    }

    // The fixed segment layout spans the whole 16-bit address space
    if (size < VM_ADDRESS_SPACE_SIZE) {
        return vm_raise(vm, VM_ERROR_MEMORY_ALLOCATION, "Memory size must be at least %u bytes", VM_ADDRESS_SPACE_SIZE);
    }

    // Allocate memory buffer
    vm->memory = (uint8_t*)malloc(size);
    if (!vm->memory) {
        return vm_raise(vm, VM_ERROR_MEMORY_ALLOCATION, "Failed to allocate %d bytes for VM memory", size);
    }
    
    // Initialize memory to zero
    memset(vm->memory, 0, size);
    vm->memory_size = size;
    
    write_block(vm, HEAP_SEGMENT_BASE, HEAP_SEGMENT_SIZE, 0, PROT_ALL);
    
    return VM_ERROR_NONE;
}

// Clean up memory resources
void memory_cleanup(VM *vm) {
    if (vm && vm->memory) {
        free(vm->memory);
        vm->memory = NULL;
        vm->memory_size = 0;
    }
}

// Check if memory address is valid
int memory_check_address(VM *vm, uint16_t address, uint16_t size) {
    // Basic bounds check without permission check
    if (!vm || !vm->memory) {
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    if (address + size > vm->memory_size) {
        return vm_raise(vm, VM_ERROR_SEGMENTATION_FAULT, "Memory access violation: address 0x%04X, size %d", address, size);
    }
    
    return VM_ERROR_NONE;
}

// Get memory pointer with bounds checking
uint8_t* memory_get_ptr(VM *vm, uint16_t address) {
    if (memory_check_address(vm, address, 1) != VM_ERROR_NONE) {
        return NULL;
    }
    return &vm->memory[address];
}

uint8_t memory_read_byte(VM *vm, uint16_t address) {
    // Check both address validity and read permission
    if (memory_check_address_permissions(vm, address, 1, PROT_READ) != VM_ERROR_NONE) {
        return 0;
    }
    
    return vm->memory[address];
}

// Write a byte to memory with permission check
void memory_write_byte(VM *vm, uint16_t address, uint8_t value) {
    // Check both address validity and write permission
    if (memory_check_address_permissions(vm, address, 1, PROT_WRITE) != VM_ERROR_NONE) {
        return;
    }
    
    vm->memory[address] = value;
}

// Read a 16-bit word from memory
uint16_t memory_read_word(VM *vm, uint16_t address) {
    // Check both address validity and read permission for 2 bytes
    if (memory_check_address_permissions(vm, address, 2, PROT_READ) != VM_ERROR_NONE) {
        return 0;
    }
    
    // Little-endian byte order
    return (uint16_t)(vm->memory[address]) |
           ((uint16_t)(vm->memory[address + 1]) << 8);
}

// Write a 16-bit word to memory with permission check
void memory_write_word(VM *vm, uint16_t address, uint16_t value) {
    // Check both address validity and write permission for 2 bytes
    if (memory_check_address_permissions(vm, address, 2, PROT_WRITE) != VM_ERROR_NONE) {
        return;
    }
    
    // Little-endian byte order
    vm->memory[address] = (uint8_t)(value & 0xFF);
    vm->memory[address + 1] = (uint8_t)((value >> 8) & 0xFF);
}

// Read a 32-bit dword from memory
uint32_t memory_read_dword(VM *vm, uint16_t address) {
    // Check both address validity and read permission for 4 bytes
    if (memory_check_address_permissions(vm, address, 4, PROT_READ) != VM_ERROR_NONE) {
        return 0;
    }
    
    // Little-endian byte order
    return (uint32_t)(vm->memory[address]) |
           ((uint32_t)(vm->memory[address + 1]) << 8) |
           ((uint32_t)(vm->memory[address + 2]) << 16) |
           ((uint32_t)(vm->memory[address + 3]) << 24);
}

// Write a 32-bit dword to memory with permission check
void memory_write_dword(VM *vm, uint16_t address, uint32_t value) {
    // Check both address validity and write permission for 4 bytes
    if (memory_check_address_permissions(vm, address, 4, PROT_WRITE) != VM_ERROR_NONE) {
        return;
    }
    
    // Little-endian byte order
    vm->memory[address] = (uint8_t)(value & 0xFF);
    vm->memory[address + 1] = (uint8_t)((value >> 8) & 0xFF);
    vm->memory[address + 2] = (uint8_t)((value >> 16) & 0xFF);
    vm->memory[address + 3] = (uint8_t)((value >> 24) & 0xFF);
}

// Copy a block of memory
int memory_copy(VM *vm, uint16_t dest, uint16_t src, uint16_t size) {
    // Check source has read permission
    if (memory_check_address_permissions(vm, src, size, PROT_READ) != VM_ERROR_NONE) {
        return vm->last_error;
    }
    
    // Check destination has write permission
    if (memory_check_address_permissions(vm, dest, size, PROT_WRITE) != VM_ERROR_NONE) {
        return vm->last_error;
    }
    
    // Handle overlapping memory blocks
    memmove(&vm->memory[dest], &vm->memory[src], size);
    return VM_ERROR_NONE;
}

// Set a block of memory to a specific value with permission check
int memory_set(VM *vm, uint16_t address, uint8_t value, uint16_t size) {
    // Check destination has write permission
    if (memory_check_address_permissions(vm, address, size, PROT_WRITE) != VM_ERROR_NONE) {
        return vm->last_error;
    }
    
    memset(&vm->memory[address], value, size);
    return VM_ERROR_NONE;
}

// Allocate zero-filled memory from the heap, returning 0 on failure
uint16_t memory_allocate(VM *vm, uint32_t size) {
    if (!vm || !vm->memory) {
        return 0;
    }

    if (size > HEAP_SEGMENT_SIZE - HEAP_HEADER_SIZE) {
        vm_raise(vm, VM_ERROR_MEMORY_ALLOCATION, "Allocation size too large: %u bytes", size);
        return 0;
    }

    uint32_t needed = (size < HEAP_MIN_ALLOC ? HEAP_MIN_ALLOC : (size + 3) & ~3u) + HEAP_HEADER_SIZE;

    for (uint32_t block = HEAP_SEGMENT_BASE; block < HEAP_END; block += block_size(vm, block)) {
        if (!block_valid(vm, block)) {
            vm_raise(vm, VM_ERROR_MEMORY_ALLOCATION, "Corrupted heap at address 0x%04X", block);
            return 0;
        }

        uint32_t available = block_size(vm, block);
        if (block_allocated(vm, block) || available < needed) {
            continue;
        }

        if (available - needed >= HEAP_HEADER_SIZE + HEAP_MIN_ALLOC) {
            write_block(vm, block + needed, available - needed, 0, PROT_ALL);
            available = needed;
        }

        write_block(vm, block, available, 1, PROT_ALL);
        memset(vm->memory + block + HEAP_HEADER_SIZE, 0, available - HEAP_HEADER_SIZE);
        return (uint16_t)(block + HEAP_HEADER_SIZE);
    }

    vm_raise(vm, VM_ERROR_MEMORY_ALLOCATION, "Out of heap memory allocating %u bytes", size);
    return 0;
}

int memory_check_address_permissions(VM *vm, uint16_t address, uint16_t size, uint8_t required_perm) {
    if (!vm || !vm->memory) {
        return VM_ERROR_INVALID_ADDRESS;
    }

    uint32_t end = (uint32_t)address + size;
    if (end > vm->memory_size) {
        return vm_raise(vm, VM_ERROR_SEGMENTATION_FAULT,
                            "Memory access violation: address 0x%04X, size %d", address, size);
    }

    // Any access touching the heap must stay inside one allocated block
    if (end > HEAP_SEGMENT_BASE && address < HEAP_END) {
        uint32_t block = find_block(vm, address);

        if (!block || !block_allocated(vm, block)) {
            return vm_raise(vm, VM_ERROR_SEGMENTATION_FAULT,
                                "Memory access to unallocated heap: address 0x%04X", address);
        }
        if (end > block + block_size(vm, block)) {
            return vm_raise(vm, VM_ERROR_SEGMENTATION_FAULT,
                                "Memory access past end of heap block: address 0x%04X, size %d", address, size);
        }

        uint8_t protection = vm->memory[block + 5];
        if ((protection & required_perm) != required_perm) {
            return vm_raise(vm, VM_ERROR_PROTECTION_FAULT,
                                "Memory protection violation: address 0x%04X, required permission 0x%02X, actual permission 0x%02X",
                                address, required_perm, protection);
        }
    }

    return VM_ERROR_NONE;
}

// Merge a free block with free neighbours on both sides
static void coalesce(VM *vm, uint32_t block) {
    uint32_t size = block_size(vm, block);

    while (block + size < HEAP_END && block_valid(vm, block + size) && !block_allocated(vm, block + size)) {
        uint32_t next = block + size;
        size += block_size(vm, next);
        memset(vm->memory + next, 0, HEAP_HEADER_SIZE);
    }
    write_block(vm, block, size, 0, PROT_ALL);

    uint32_t previous = 0;
    for (uint32_t b = HEAP_SEGMENT_BASE; b < block && block_valid(vm, b); b += block_size(vm, b)) {
        previous = b;
    }
    if (previous && !block_allocated(vm, previous)) {
        write_block(vm, previous, block_size(vm, previous) + size, 0, PROT_ALL);
        memset(vm->memory + block, 0, HEAP_HEADER_SIZE);
    }
}

// Free an allocated block given the address returned by memory_allocate
int memory_free(VM *vm, uint16_t address) {
    if (!vm || !vm->memory) {
        return VM_ERROR_INVALID_ADDRESS;
    }

    if (address < HEAP_SEGMENT_BASE + HEAP_HEADER_SIZE || address >= HEAP_END) {
        return vm_raise(vm, VM_ERROR_INVALID_ADDRESS, "Invalid heap address for free: 0x%04X", address);
    }

    uint32_t block = find_block(vm, address);
    if (block && !block_allocated(vm, block)) {
        return vm_raise(vm, VM_ERROR_INVALID_ADDRESS, "Double free detected at 0x%04X", address);
    }
    if (!block || block + HEAP_HEADER_SIZE != address) {
        return vm_raise(vm, VM_ERROR_INVALID_ADDRESS,
                            "Address 0x%04X is not the start of an allocated block", address);
    }

    coalesce(vm, block);
    return VM_ERROR_NONE;
}

// Set the protection flags of an allocated block
int memory_protect(VM *vm, uint16_t address, uint8_t flags) {
    if (!vm || !vm->memory) {
        return VM_ERROR_INVALID_ADDRESS;
    }

    uint32_t block = find_block(vm, address);
    if (!block || !block_allocated(vm, block) || block + HEAP_HEADER_SIZE != address) {
        return vm_raise(vm, VM_ERROR_INVALID_ADDRESS,
                            "Address 0x%04X is not the start of an allocated block", address);
    }

    vm->memory[block + 5] = flags & PROT_ALL;
    return VM_ERROR_NONE;
}

int memory_might_be_string(VM *vm, uint16_t addr) {
    if (!vm || addr >= vm->memory_size) {
        return 0;
    }
    
    // Check if address is in data or heap segment
    if ((addr >= DATA_SEGMENT_BASE && addr < DATA_SEGMENT_BASE + DATA_SEGMENT_SIZE) ||
        (addr >= HEAP_SEGMENT_BASE && addr < HEAP_SEGMENT_BASE + HEAP_SEGMENT_SIZE)) {
        
        // Try to read potential string - limit to reasonable length
        const int MAX_STRING_CHECK = 64;
        int printable_chars = 0;
        int total_chars = 0;
        
        for (int i = 0; i < MAX_STRING_CHECK; i++) {
            if (addr + i >= vm->memory_size) {
                break;
            }
            
            uint8_t c = vm->memory[addr + i];
            
            // If we hit null terminator and have seen some printable chars, it's likely a string
            if (c == 0 && printable_chars > 0) {
                return 1;
            }
            
            // Count printable characters (ASCII 32-126 plus common control chars)
            if ((c >= 32 && c <= 126) || c == '\n' || c == '\r' || c == '\t') {
                printable_chars++;
            }
            
            total_chars++;
            
            // If we've seen some characters but ratio of printable is low, probably not a string
            if (total_chars > 3 && printable_chars < total_chars / 2) {
                return 0;
            }
        }
        
        // If we've found several printable characters, might be a string
        return (printable_chars > 3);
    }
    
    return 0;
}

char* memory_extract_string(VM *vm, uint16_t addr, int max_length) {
    if (!vm || addr >= vm->memory_size) {
        return NULL;
    }
    
    // Find string length (up to max_length)
    int length = 0;
    while (length < max_length) {
        if (addr + length >= vm->memory_size) {
            break;
        }
        
        if (vm->memory[addr + length] == 0) {
            break;
        }
        
        length++;
    }
    
    // Allocate and copy the string
    char* result = (char*)malloc(length + 1);
    if (!result) {
        return NULL;
    }
    
    for (int i = 0; i < length; i++) {
        char c = (char)vm->memory[addr + i];
        
        // Replace control characters with spaces for display
        if (c < 32 && c != '\n' && c != '\r' && c != '\t') {
            c = ' ';
        }
        
        result[i] = c;
    }
    
    result[length] = '\0';
    return result;
}