#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory.h"
#include "vm.h"

// Initialize memory for the VM
int memory_init(VM *vm, uint32_t size) {
    if (!vm) {
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    // Allocate memory buffer
    vm->memory = (uint8_t*)malloc(size);
    if (!vm->memory) {
        vm->last_error = VM_ERROR_MEMORY_ALLOCATION;
        snprintf(vm->error_message, sizeof(vm->error_message), 
                 "Failed to allocate %d bytes for VM memory", size);
        return VM_ERROR_MEMORY_ALLOCATION;
    }
    
    // Initialize memory to zero
    memset(vm->memory, 0, size);
    vm->memory_size = size;
    
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
    if (!vm || !vm->memory) {
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    // Check if address is within bounds
    if (address + size > vm->memory_size) {
        vm->last_error = VM_ERROR_SEGMENTATION_FAULT;
        snprintf(vm->error_message, sizeof(vm->error_message), 
                 "Memory access violation: address 0x%04X, size %d", address, size);
        return VM_ERROR_SEGMENTATION_FAULT;
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

// Read a byte from memory
uint8_t memory_read_byte(VM *vm, uint16_t address) {
    if (memory_check_address(vm, address, 1) != VM_ERROR_NONE) {
        return 0;
    }
    return vm->memory[address];
}

// Write a byte to memory
void memory_write_byte(VM *vm, uint16_t address, uint8_t value) {
    if (memory_check_address(vm, address, 1) != VM_ERROR_NONE) {
        return;
    }
    vm->memory[address] = value;
}

// Read a 16-bit word from memory
uint16_t memory_read_word(VM *vm, uint16_t address) {
    if (memory_check_address(vm, address, 2) != VM_ERROR_NONE) {
        return 0;
    }
    
    // Little-endian byte order
    return (uint16_t)(vm->memory[address]) |
           ((uint16_t)(vm->memory[address + 1]) << 8);
}

// Write a 16-bit word to memory
void memory_write_word(VM *vm, uint16_t address, uint16_t value) {
    if (memory_check_address(vm, address, 2) != VM_ERROR_NONE) {
        return;
    }
    
    // Little-endian byte order
    vm->memory[address] = (uint8_t)(value & 0xFF);
    vm->memory[address + 1] = (uint8_t)((value >> 8) & 0xFF);
}

// Read a 32-bit dword from memory
uint32_t memory_read_dword(VM *vm, uint16_t address) {
    if (memory_check_address(vm, address, 4) != VM_ERROR_NONE) {
        return 0;
    }
    
    // Little-endian byte order
    return (uint32_t)(vm->memory[address]) |
           ((uint32_t)(vm->memory[address + 1]) << 8) |
           ((uint32_t)(vm->memory[address + 2]) << 16) |
           ((uint32_t)(vm->memory[address + 3]) << 24);
}

// Write a 32-bit dword to memory
void memory_write_dword(VM *vm, uint16_t address, uint32_t value) {
    if (memory_check_address(vm, address, 4) != VM_ERROR_NONE) {
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
    // Check source and destination addresses
    if (memory_check_address(vm, src, size) != VM_ERROR_NONE) {
        return VM_ERROR_SEGMENTATION_FAULT;
    }
    
    if (memory_check_address(vm, dest, size) != VM_ERROR_NONE) {
        return VM_ERROR_SEGMENTATION_FAULT;
    }
    
    // Handle overlapping memory blocks
    memmove(&vm->memory[dest], &vm->memory[src], size);
    return VM_ERROR_NONE;
}

// Set a block of memory to a specific value
int memory_set(VM *vm, uint16_t address, uint8_t value, uint16_t size) {
    if (memory_check_address(vm, address, size) != VM_ERROR_NONE) {
        return VM_ERROR_SEGMENTATION_FAULT;
    }
    
    memset(&vm->memory[address], value, size);
    return VM_ERROR_NONE;
}

// Basic heap memory allocation (very simplified)
uint16_t memory_allocate(VM *vm, uint16_t size) {
    // TODO:  implementation, you would need a proper heap allocator
    // This is a simplified placeholder
    static uint16_t next_free = HEAP_SEGMENT_BASE;
    
    uint16_t allocation_address = next_free;
    next_free += size;
    
    // Check if we've run out of heap space
    if (next_free > HEAP_SEGMENT_BASE + HEAP_SEGMENT_SIZE) {
        vm->last_error = VM_ERROR_MEMORY_ALLOCATION;
        snprintf(vm->error_message, sizeof(vm->error_message), 
                 "Heap memory exhausted");
        return 0;
    }
    
    // Zero the allocated memory
    memory_set(vm, allocation_address, 0, size);
    
    return allocation_address;
}

// Basic heap memory freeing (placeholder)
int memory_free(VM *vm, uint16_t address) {
    // TODO: implementation would need a proper heap manager
    // with free lists or garbage collection
    
    // Check if address is in the heap segment
    if (address < HEAP_SEGMENT_BASE || 
        address >= HEAP_SEGMENT_BASE + HEAP_SEGMENT_SIZE) {
        vm->last_error = VM_ERROR_INVALID_ADDRESS;
        snprintf(vm->error_message, sizeof(vm->error_message), 
                 "Invalid heap address 0x%04X for free operation", address);
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    // For now, we don't actually do anything
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