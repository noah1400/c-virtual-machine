#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "binfmt.h"
#include "memory.h"
#include "vm.h"

// Heap blocks are tracked outside VM memory, sorted by address. A guard gap that no access may touch
// precedes every block, and freed blocks stay listed until their space is reused, so that a second
// free can be told apart from a bad pointer.
#define HEAP_GUARD      8u
#define HEAP_ALIGNMENT  8u

struct HeapBlock {
    uint32_t start;
    uint32_t size;
    uint8_t protection;
    uint8_t allocated;
};

int memory_init(VM *vm, uint32_t size) {
    if (size < VM_MIN_MEMORY_SIZE) {
        return vm_raise(vm, VM_ERROR_MEMORY_ALLOCATION, "Memory size must be at least %u bytes", VM_MIN_MEMORY_SIZE);
    }

    vm->memory = calloc(size, 1);
    if (!vm->memory) {
        return vm_raise(vm, VM_ERROR_MEMORY_ALLOCATION, "Failed to allocate %u bytes for VM memory", size);
    }
    vm->memory_size = size;
    return VM_ERROR_NONE;
}

void memory_cleanup(VM *vm) {
    free(vm->memory);
    vm->memory = NULL;
    vm->memory_size = 0;
    free(vm->heap_blocks);
    vm->heap_blocks = NULL;
    vm->heap_block_count = 0;
    vm->heap_block_capacity = 0;
}

void memory_heap_reset(VM *vm) {
    vm->heap_block_count = 0;
}

static int in_heap(const VM *vm, uint32_t address, uint32_t size) {
    return (uint64_t)address + size > vm->control[CR_HEAPLO] && address < vm->control[CR_HEAPHI];
}

// Index of the last block that starts at or before address, or -1
static long block_before(const VM *vm, uint32_t address) {
    long low = 0, high = (long)vm->heap_block_count - 1, found = -1;
    while (low <= high) {
        long middle = low + (high - low) / 2;
        if (vm->heap_blocks[middle].start <= address) {
            found = middle;
            low = middle + 1;
        } else {
            high = middle - 1;
        }
    }
    return found;
}

// The allocated block whose data contains address, or NULL
static struct HeapBlock *find_block(const VM *vm, uint32_t address) {
    long index = block_before(vm, address);
    struct HeapBlock *block = index >= 0 ? &vm->heap_blocks[index] : NULL;
    return block && block->allocated && address - block->start < block->size ? block : NULL;
}

// Raises a memory fault and records the address that caused it in FADDR
static int memory_fault(VM *vm, int code, uint32_t address, const char *format, ...) {
    char message[sizeof(vm->error_message)];
    va_list args;

    if (vm->last_error == VM_ERROR_NONE) {
        vm->control[CR_FADDR] = address;
    }
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    return vm_raise(vm, code, "%s", message);
}

int memory_check_address(VM *vm, uint32_t address, uint32_t size) {
    if (address > vm->memory_size || size > vm->memory_size - address) {
        return memory_fault(vm, VM_ERROR_SEGMENTATION_FAULT, address,
                            "Memory access violation: address 0x%04X, size %u", address, size);
    }
    return VM_ERROR_NONE;
}

uint8_t memory_read_byte(VM *vm, uint32_t address) {
    if (memory_check_address_permissions(vm, address, 1, PROT_READ) != VM_ERROR_NONE) {
        return 0;
    }
    return vm->memory[address];
}

void memory_write_byte(VM *vm, uint32_t address, uint8_t value) {
    if (memory_check_address_permissions(vm, address, 1, PROT_WRITE) == VM_ERROR_NONE) {
        vm->memory[address] = value;
    }
}

uint16_t memory_read_word(VM *vm, uint32_t address) {
    if (memory_check_address_permissions(vm, address, 2, PROT_READ) != VM_ERROR_NONE) {
        return 0;
    }
    return read_le16(vm->memory + address);
}

void memory_write_word(VM *vm, uint32_t address, uint16_t value) {
    if (memory_check_address_permissions(vm, address, 2, PROT_WRITE) == VM_ERROR_NONE) {
        write_le16(vm->memory + address, value);
    }
}

uint32_t memory_read_dword(VM *vm, uint32_t address) {
    if (memory_check_address_permissions(vm, address, 4, PROT_READ) != VM_ERROR_NONE) {
        return 0;
    }
    return read_le32(vm->memory + address);
}

void memory_write_dword(VM *vm, uint32_t address, uint32_t value) {
    if (memory_check_address_permissions(vm, address, 4, PROT_WRITE) == VM_ERROR_NONE) {
        write_le32(vm->memory + address, value);
    }
}

// Copies with memmove semantics so overlapping ranges are safe
int memory_copy(VM *vm, uint32_t dest, uint32_t src, uint32_t size) {
    if (memory_check_address_permissions(vm, src, size, PROT_READ) != VM_ERROR_NONE ||
        memory_check_address_permissions(vm, dest, size, PROT_WRITE) != VM_ERROR_NONE) {
        return vm->last_error;
    }
    memmove(vm->memory + dest, vm->memory + src, size);
    return VM_ERROR_NONE;
}

int memory_set(VM *vm, uint32_t address, uint8_t value, uint32_t size) {
    if (memory_check_address_permissions(vm, address, size, PROT_WRITE) != VM_ERROR_NONE) {
        return vm->last_error;
    }
    memset(vm->memory + address, value, size);
    return VM_ERROR_NONE;
}

// Adds an allocated block, forgetting freed blocks whose space it reuses
static int insert_block(VM *vm, uint32_t start, uint32_t size) {
    uint32_t kept = 0;
    for (uint32_t i = 0; i < vm->heap_block_count; i++) {
        struct HeapBlock block = vm->heap_blocks[i];
        int reused = (uint64_t)block.start + block.size > start - HEAP_GUARD && block.start < (uint64_t)start + size;
        if (block.allocated || !reused) {
            vm->heap_blocks[kept++] = block;
        }
    }
    vm->heap_block_count = kept;

    if (kept == vm->heap_block_capacity) {
        uint32_t capacity = kept ? kept * 2 : 16;
        struct HeapBlock *blocks = realloc(vm->heap_blocks, capacity * sizeof(struct HeapBlock));
        if (!blocks) {
            return 0;
        }
        vm->heap_blocks = blocks;
        vm->heap_block_capacity = capacity;
    }

    uint32_t position = 0;
    while (position < kept && vm->heap_blocks[position].start < start) {
        position++;
    }
    memmove(&vm->heap_blocks[position + 1], &vm->heap_blocks[position], (kept - position) * sizeof(struct HeapBlock));
    vm->heap_blocks[position] = (struct HeapBlock){ start, size, PROT_ALL, 1 };
    vm->heap_block_count++;
    return 1;
}

// Allocate zero-filled memory from the heap, returning 0 on failure
uint32_t memory_allocate(VM *vm, uint32_t size) {
    uint32_t low = vm->control[CR_HEAPLO], high = vm->control[CR_HEAPHI];
    uint64_t needed = size < HEAP_ALIGNMENT ? HEAP_ALIGNMENT : ((uint64_t)size + HEAP_ALIGNMENT - 1) & ~(uint64_t)(HEAP_ALIGNMENT - 1);

    if (high <= low || needed + HEAP_GUARD > high - low) {
        vm_raise(vm, VM_ERROR_MEMORY_ALLOCATION, "Allocation size too large: %u bytes", size);
        return 0;
    }

    // First fit in the gaps between allocated blocks
    uint64_t candidate = (((uint64_t)low + HEAP_ALIGNMENT - 1) & ~(uint64_t)(HEAP_ALIGNMENT - 1)) + HEAP_GUARD;
    for (uint32_t i = 0; i < vm->heap_block_count; i++) {
        const struct HeapBlock *block = &vm->heap_blocks[i];
        if (!block->allocated) {
            continue;
        }
        if (candidate + needed + HEAP_GUARD <= block->start) {
            break;
        }
        candidate = (uint64_t)block->start + block->size + HEAP_GUARD;
    }
    if (candidate + needed > high) {
        vm_raise(vm, VM_ERROR_MEMORY_ALLOCATION, "Out of heap memory allocating %u bytes", size);
        return 0;
    }
    if (!insert_block(vm, (uint32_t)candidate, (uint32_t)needed)) {
        vm_raise(vm, VM_ERROR_MEMORY_ALLOCATION, "Out of host memory for heap blocks");
        return 0;
    }

    memset(vm->memory + candidate, 0, needed);
    return (uint32_t)candidate;
}

int memory_check_address_permissions(VM *vm, uint32_t address, uint32_t size, uint8_t required_perm) {
    if (memory_check_address(vm, address, size) != VM_ERROR_NONE) {
        return VM_ERROR_SEGMENTATION_FAULT;
    }

    // Any access touching the heap must stay inside one allocated block
    if (size > 0 && in_heap(vm, address, size)) {
        struct HeapBlock *block = find_block(vm, address);

        if (!block) {
            return memory_fault(vm, VM_ERROR_SEGMENTATION_FAULT, address,
                                "Memory access to unallocated heap: address 0x%04X", address);
        }
        if (size > block->start + block->size - address) {
            return memory_fault(vm, VM_ERROR_SEGMENTATION_FAULT, address,
                                "Memory access past end of heap block: address 0x%04X, size %u", address, size);
        }
        if ((block->protection & required_perm) != required_perm) {
            return memory_fault(vm, VM_ERROR_PROTECTION_FAULT, address,
                                "Memory protection violation: address 0x%04X, required permission 0x%02X, actual permission 0x%02X",
                                address, required_perm, block->protection);
        }
    }

    return VM_ERROR_NONE;
}

static void count_gap(uint64_t start, uint64_t end, uint32_t *free_bytes, uint32_t *largest_free) {
    if (end > start + HEAP_GUARD) {
        uint32_t payload = (uint32_t)(end - start - HEAP_GUARD) & ~(HEAP_ALIGNMENT - 1);
        *free_bytes += payload;
        if (payload > *largest_free) {
            *largest_free = payload;
        }
    }
}

// Total and largest free payload bytes in the heap
void memory_heap_stats(const VM *vm, uint32_t *free_bytes, uint32_t *largest_free) {
    uint64_t gap = vm->control[CR_HEAPLO];

    *free_bytes = 0;
    *largest_free = 0;
    for (uint32_t i = 0; i < vm->heap_block_count; i++) {
        const struct HeapBlock *block = &vm->heap_blocks[i];
        if (block->allocated) {
            count_gap(gap, (uint64_t)block->start - HEAP_GUARD, free_bytes, largest_free);
            gap = (uint64_t)block->start + block->size;
        }
    }
    count_gap(gap, vm->control[CR_HEAPHI], free_bytes, largest_free);
}

// Free an allocated block given the address returned by memory_allocate
int memory_free(VM *vm, uint32_t address) {
    long index = block_before(vm, address);
    struct HeapBlock *block = index >= 0 ? &vm->heap_blocks[index] : NULL;

    if (!in_heap(vm, address, 1)) {
        return vm_raise(vm, VM_ERROR_INVALID_ADDRESS, "Invalid heap address for free: 0x%04X", address);
    }
    if (block && block->start == address && !block->allocated) {
        return vm_raise(vm, VM_ERROR_INVALID_ADDRESS, "Double free detected at 0x%04X", address);
    }
    if (!block || block->start != address) {
        return vm_raise(vm, VM_ERROR_INVALID_ADDRESS,
                        "Address 0x%04X is not the start of an allocated block", address);
    }
    block->allocated = 0;
    return VM_ERROR_NONE;
}

// Set the protection flags of an allocated block
int memory_protect(VM *vm, uint32_t address, uint8_t flags) {
    struct HeapBlock *block = find_block(vm, address);
    if (!block || block->start != address) {
        return vm_raise(vm, VM_ERROR_INVALID_ADDRESS,
                        "Address 0x%04X is not the start of an allocated block", address);
    }

    block->protection = flags & PROT_ALL;
    return VM_ERROR_NONE;
}
