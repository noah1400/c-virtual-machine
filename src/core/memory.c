#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory.h"
#include "vm.h"

// Memory block header structure - must be kept small
typedef struct {
    uint16_t magic;      // Magic number for validation (0xABCD)
    uint16_t size;       // Size of the block including header
    uint8_t  is_free;    // Flag indicating if block is free
    uint8_t  protection; // Protection flags
    uint16_t next;       // Offset to next block or 0 if last block
} MemBlock;

#define MEMBLOCK_MAGIC 0xABCD
#define MEMBLOCK_HEADER_SIZE sizeof(MemBlock)
#define MIN_ALLOC_SIZE 8

// Protection flags (will be used when we implement PROTECT)
#define PROT_NONE 0x00
#define PROT_READ 0x01
#define PROT_WRITE 0x02
#define PROT_EXEC 0x04
#define PROT_ALL (PROT_READ | PROT_WRITE | PROT_EXEC)

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
    
    // Initialize heap - create initial free block at HEAP_SEGMENT_BASE
    MemBlock* init_block = (MemBlock*)(vm->memory + HEAP_SEGMENT_BASE);
    init_block->magic = MEMBLOCK_MAGIC;
    init_block->size = HEAP_SEGMENT_SIZE;
    init_block->is_free = 1;
    init_block->protection = PROT_ALL;
    init_block->next = 0;  // No next block
    
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

// Dump heap state for debugging
void dump_heap(VM *vm) {
    printf("Heap state:\n");
    uint16_t block_addr = HEAP_SEGMENT_BASE;
    
    while (block_addr < HEAP_SEGMENT_BASE + HEAP_SEGMENT_SIZE) {
        MemBlock* block = (MemBlock*)(vm->memory + block_addr);
        
        // Check if this looks like a valid block
        if (block->magic != MEMBLOCK_MAGIC) {
            printf("  Invalid block at 0x%04X\n", block_addr);
            break;
        }
        
        printf("  Block at 0x%04X: size=%d, %s, next=%d\n", 
               block_addr, block->size, 
               block->is_free ? "FREE" : "USED",
               block->next);
        
        // If no next block, we're done
        if (block->next == 0) break;
        block_addr += block->next;
    }
}

// Check if memory address is valid
int memory_check_address(VM *vm, uint16_t address, uint16_t size) {
    // Basic bounds check without permission check
    if (!vm || !vm->memory) {
        return VM_ERROR_INVALID_ADDRESS;
    }
    
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

// Allocate memory from the heap
uint16_t memory_allocate(VM *vm, uint16_t size) {
    if (!vm || !vm->memory) {
        return 0;
    }
    
    // Ensure minimum allocation size
    if (size < MIN_ALLOC_SIZE) {
        size = MIN_ALLOC_SIZE;
    }
    
    // Align size to 4 bytes for better memory efficiency
    size = (size + 3) & ~3;
    
    // Add header size to allocation
    uint16_t total_size = size + MEMBLOCK_HEADER_SIZE;
    
    // Find a free block that's large enough (first fit)
    uint16_t block_addr = HEAP_SEGMENT_BASE;
    uint16_t prev_addr = 0;
    
    while (block_addr < HEAP_SEGMENT_BASE + HEAP_SEGMENT_SIZE) {
        MemBlock* block = (MemBlock*)(vm->memory + block_addr);
        
        // Check if this is a valid block
        if (block->magic != MEMBLOCK_MAGIC) {
            vm->last_error = VM_ERROR_MEMORY_ALLOCATION;
            snprintf(vm->error_message, sizeof(vm->error_message), 
                     "Corrupted heap at address 0x%04X", block_addr);
            return 0;
        }
        
        // Check if block is free and large enough
        if (block->is_free && block->size >= total_size) {
            
            // Check if we need to split the block
            if (block->size >= total_size + MEMBLOCK_HEADER_SIZE + MIN_ALLOC_SIZE) {
                // Split the block
                uint16_t new_block_addr = block_addr + total_size;
                MemBlock* new_block = (MemBlock*)(vm->memory + new_block_addr);
                
                // Initialize the new block
                new_block->magic = MEMBLOCK_MAGIC;
                new_block->size = block->size - total_size;
                new_block->is_free = 1;
                new_block->protection = PROT_ALL;
                new_block->next = block->next == 0 ? 0 : block->next - total_size;
                
                // Update current block
                block->size = total_size;
                block->next = total_size;
            }
            
            // Mark block as allocated
            block->is_free = 0;
            
            // Calculate the address after the header (for the user data)
            uint16_t data_addr = block_addr + MEMBLOCK_HEADER_SIZE;
            
            // Return address after the header
            return data_addr;
        }
        
        // Move to next block
        prev_addr = block_addr;
        if (block->next == 0) {
            break;
        }
        block_addr += block->next;
    }
    
    vm->last_error = VM_ERROR_MEMORY_ALLOCATION;
    snprintf(vm->error_message, sizeof(vm->error_message), 
             "Failed to allocate %d bytes from heap", size);
    return 0;
}

// Find the block header for a given data address
static MemBlock* find_block_header(VM *vm, uint16_t address) {
    // The heap range is from HEAP_SEGMENT_BASE to HEAP_SEGMENT_BASE + HEAP_SEGMENT_SIZE
    if (address < HEAP_SEGMENT_BASE || 
        address >= HEAP_SEGMENT_BASE + HEAP_SEGMENT_SIZE) {
        return NULL;
    }
    
    // Scan through blocks to find the one containing this address
    uint16_t block_addr = HEAP_SEGMENT_BASE;
    
    while (block_addr < HEAP_SEGMENT_BASE + HEAP_SEGMENT_SIZE) {
        MemBlock* block = (MemBlock*)(vm->memory + block_addr);
        
        // Check if this is a valid block
        if (block->magic != MEMBLOCK_MAGIC) {
            return NULL;
        }
        
        // Calculate the data area for this block
        uint16_t data_addr = block_addr + MEMBLOCK_HEADER_SIZE;
        uint16_t block_end = block_addr + block->size;
        
        // Check if the requested address is in this block's data area
        if (address >= data_addr && address < block_end) {
            return block;
        }
        
        // Move to next block
        if (block->next == 0) {
            break;
        }
        block_addr += block->next;
    }
    
    return NULL;
}

static MemBlock* find_block_containing(VM *vm, uint16_t address) {
    if (!vm || !vm->memory) {
        return NULL;
    }
    
    // Check if address is in heap segment
    if (address < HEAP_SEGMENT_BASE || 
        address >= HEAP_SEGMENT_BASE + HEAP_SEGMENT_SIZE) {
        return NULL;
    }
    
    // Scan through blocks to find the one containing this address
    uint16_t block_addr = HEAP_SEGMENT_BASE;
    
    while (block_addr < HEAP_SEGMENT_BASE + HEAP_SEGMENT_SIZE) {
        MemBlock* block = (MemBlock*)(vm->memory + block_addr);
        
        // Check if this is a valid block
        if (block->magic != MEMBLOCK_MAGIC) {
            return NULL;
        }
        
        // Calculate the data area for this block
        uint16_t data_start = block_addr + MEMBLOCK_HEADER_SIZE;
        uint16_t block_end = block_addr + block->size;
        
        // Check if the requested address is in this block
        if (address >= data_start && address < block_end) {
            return block;
        }
        
        // Move to next block
        if (block->next == 0) {
            break;
        }
        block_addr += block->next;
    }
    
    return NULL;
}

int memory_check_address_permissions(VM *vm, uint16_t address, uint16_t size, uint8_t required_perm) {
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
    
    // For heap memory, check if it's allocated and has appropriate permissions
    if (address >= HEAP_SEGMENT_BASE && 
        address < HEAP_SEGMENT_BASE + HEAP_SEGMENT_SIZE) {
        
        // Check both the start and end addresses
        MemBlock* start_block = find_block_containing(vm, address);
        MemBlock* end_block = find_block_containing(vm, address + size - 1);
        
        // If not found, it's not in an allocated block
        if (!start_block || !end_block) {
            vm->last_error = VM_ERROR_SEGMENTATION_FAULT;
            snprintf(vm->error_message, sizeof(vm->error_message), 
                     "Memory access to unallocated heap: address 0x%04X", address);
            return VM_ERROR_SEGMENTATION_FAULT;
        }
        
        // If spans multiple blocks, error
        if (start_block != end_block) {
            vm->last_error = VM_ERROR_SEGMENTATION_FAULT;
            snprintf(vm->error_message, sizeof(vm->error_message), 
                     "Memory access spans multiple blocks: address 0x%04X, size %d", address, size);
            return VM_ERROR_SEGMENTATION_FAULT;
        }
        
        // If the block is free, error
        if (start_block->is_free) {
            vm->last_error = VM_ERROR_SEGMENTATION_FAULT;
            snprintf(vm->error_message, sizeof(vm->error_message), 
                     "Memory access to freed block: address 0x%04X", address);
            return VM_ERROR_SEGMENTATION_FAULT;
        }
        
        // Check protection flags
        if ((start_block->protection & required_perm) != required_perm) {
            vm->last_error = VM_ERROR_PROTECTION_FAULT;
            snprintf(vm->error_message, sizeof(vm->error_message), 
                     "Memory protection violation: address 0x%04X, required permission 0x%02X, actual permission 0x%02X", 
                     address, required_perm, start_block->protection);
            return VM_ERROR_PROTECTION_FAULT;
        }
    }
    
    return VM_ERROR_NONE;
}

// Free allocated memory
int memory_free(VM *vm, uint16_t address) {
    if (!vm || !vm->memory) {
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    // Check if address is in heap segment
    if (address < HEAP_SEGMENT_BASE || 
        address >= HEAP_SEGMENT_BASE + HEAP_SEGMENT_SIZE) {
        vm->last_error = VM_ERROR_INVALID_ADDRESS;
        snprintf(vm->error_message, sizeof(vm->error_message), 
                 "Invalid heap address for free: 0x%04X", address);
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    // Find the block header for this address
    MemBlock* block = find_block_header(vm, address);
    if (!block) {
        vm->last_error = VM_ERROR_INVALID_ADDRESS;
        snprintf(vm->error_message, sizeof(vm->error_message), 
                 "Address 0x%04X not within any allocated block", address);
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    // Check if block is already free
    if (block->is_free) {
        vm->last_error = VM_ERROR_INVALID_ADDRESS;
        snprintf(vm->error_message, sizeof(vm->error_message), 
                 "Double free detected at 0x%04X", address);
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    // Mark block as free
    block->is_free = 1;
    
    return VM_ERROR_NONE;
}

// Set memory protection (will be implemented when PROTECT is supported)
int memory_protect(VM *vm, uint16_t address, uint8_t flags) {

    if (!vm || !vm->memory) {
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    // Check if address is in heap segment
    if (address < HEAP_SEGMENT_BASE || 
        address >= HEAP_SEGMENT_BASE + HEAP_SEGMENT_SIZE) {
        vm->last_error = VM_ERROR_INVALID_ADDRESS;
        snprintf(vm->error_message, sizeof(vm->error_message), 
                 "Invalid heap address for protect: 0x%04X", address);
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    // Find the block header for this address
    MemBlock* block = find_block_header(vm, address);
    if (!block) {
        vm->last_error = VM_ERROR_INVALID_ADDRESS;
        snprintf(vm->error_message, sizeof(vm->error_message), 
                 "Address 0x%04X not within any allocated block", address);
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    // Set the protection flags
    block->protection = flags;
    
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