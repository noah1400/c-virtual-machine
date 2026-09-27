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

static inline int in_heap(const VM *vm, uint32_t address, uint32_t size) {
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

// Any access touching the heap must stay inside one allocated block that permits it
static int heap_check(VM *vm, uint32_t address, uint32_t size, uint8_t access) {
    if (size == 0 || !in_heap(vm, address, size)) {
        return VM_ERROR_NONE;
    }

    struct HeapBlock *block = find_block(vm, address);
    if (!block) {
        return memory_fault(vm, VM_ERROR_SEGMENTATION_FAULT, address,
                            "Memory access to unallocated heap: address 0x%04X", address);
    }
    if (size > block->start + block->size - address) {
        return memory_fault(vm, VM_ERROR_SEGMENTATION_FAULT, address,
                            "Memory access past end of heap block: address 0x%04X, size %u", address, size);
    }
    if ((block->protection & access) != access) {
        return memory_fault(vm, VM_ERROR_PROTECTION_FAULT, address,
                            "Memory protection violation: address 0x%04X, required permission 0x%02X, actual permission 0x%02X",
                            address, access, block->protection);
    }
    return VM_ERROR_NONE;
}

static int read_physical(const VM *vm, uint32_t address, uint32_t *value) {
    if ((uint64_t)address + 4 > vm->memory_size) {
        return 0;
    }
    *value = read_le32(vm->memory + address);
    return 1;
}

enum { LOOKUP_OK, LOOKUP_PAGE_FAULT, LOOKUP_OUTSIDE };

// Walks the page directory at PTB and one page table; on a page fault *detail holds the ECODE bits
static int lookup(const VM *vm, uint32_t address, uint8_t access, int user, uint32_t *physical, uint32_t *detail) {
    uint32_t entry;

    *detail = (user ? FAULT_USER : 0) | (access == PROT_WRITE ? FAULT_WRITE : 0) |
              (access == PROT_EXEC ? FAULT_FETCH : 0);
    if (!read_physical(vm, (vm->control[CR_PTB] & ~(VM_PAGE_SIZE - 1)) + (address >> 22) * 4, &entry)) {
        return LOOKUP_OUTSIDE;
    }
    if (!(entry & PAGE_PRESENT)) {
        return LOOKUP_PAGE_FAULT;
    }
    if (!read_physical(vm, (entry & ~(VM_PAGE_SIZE - 1)) + ((address >> 12) & 0x3FF) * 4, &entry)) {
        return LOOKUP_OUTSIDE;
    }
    if (!(entry & PAGE_PRESENT)) {
        return LOOKUP_PAGE_FAULT;
    }
    if ((user && !(entry & PAGE_USER)) || (access == PROT_WRITE && !(entry & PAGE_WRITE)) ||
        (access == PROT_EXEC && !(entry & PAGE_EXEC))) {
        *detail |= FAULT_PRESENT;
        return LOOKUP_PAGE_FAULT;
    }
    *physical = (entry & ~(VM_PAGE_SIZE - 1)) | (address & (VM_PAGE_SIZE - 1));
    return LOOKUP_OK;
}

// Most accesses need no translation and miss the heap, so they can use physical memory directly
static inline uint8_t *direct(const VM *vm, uint32_t address, uint32_t size) {
    if (vm->control[CR_PTB] || (uint64_t)address + size > vm->memory_size || in_heap(vm, address, size)) {
        return NULL;
    }
    return vm->memory + address;
}

static inline void copy_bytes(uint8_t *buffer, uint8_t *memory, uint32_t size, uint8_t access) {
    if (!buffer) {
        return;
    }
    if (access == PROT_WRITE) {
        memcpy(memory, buffer, size);
    } else {
        memcpy(buffer, memory, size);
    }
}

// Checks an access of size bytes at a virtual address against the heap rules, the page tables and
// the bounds of memory. With a buffer it also copies: out of it for writes, into it otherwise.
static int access_range(VM *vm, uint32_t address, uint8_t *buffer, uint32_t size, uint8_t access, int heap_rules) {
    static const char *const verbs[] = { [PROT_READ] = "reading", [PROT_WRITE] = "writing", [PROT_EXEC] = "fetching" };

    if (size == 0) {
        return VM_ERROR_NONE;
    }
    if (heap_rules && heap_check(vm, address, size, access) != VM_ERROR_NONE) {
        return vm->last_error;
    }
    if (!vm->control[CR_PTB]) {
        if ((uint64_t)address + size > vm->memory_size) {
            return memory_fault(vm, VM_ERROR_SEGMENTATION_FAULT, address,
                                "Memory access violation: address 0x%04X, size %u", address, size);
        }
        copy_bytes(buffer, vm->memory + address, size, access);
        return VM_ERROR_NONE;
    }

    int user = !(vm->registers[R4_SR] & SYS_FLAG);
    for (uint32_t done = 0; done < size;) {
        uint32_t virtual = address + done, physical = 0, detail;
        uint32_t chunk = VM_PAGE_SIZE - (virtual & (VM_PAGE_SIZE - 1));
        if (chunk > size - done) {
            chunk = size - done;
        }
        if (done > 0 && virtual == 0) {
            return memory_fault(vm, VM_ERROR_SEGMENTATION_FAULT, address, "Memory access wraps around at 0x%08X", address);
        }
        switch (lookup(vm, virtual, access, user, &physical, &detail)) {
            case LOOKUP_PAGE_FAULT:
                if (vm->last_error == VM_ERROR_NONE) {
                    vm->control[CR_ECODE] = detail;
                }
                return memory_fault(vm, VM_ERROR_PAGE_FAULT, virtual, "Page fault %s 0x%08X", verbs[access], virtual);
            case LOOKUP_OUTSIDE:
                return memory_fault(vm, VM_ERROR_SEGMENTATION_FAULT, virtual,
                                    "Page table for 0x%08X lies outside memory", virtual);
        }
        if ((uint64_t)physical + chunk > vm->memory_size) {
            return memory_fault(vm, VM_ERROR_SEGMENTATION_FAULT, virtual,
                                "Memory access violation: 0x%08X maps outside memory", virtual);
        }
        copy_bytes(buffer ? buffer + done : NULL, vm->memory + physical, chunk, access);
        done += chunk;
    }
    return VM_ERROR_NONE;
}

int memory_check_range(VM *vm, uint32_t address, uint32_t size, uint8_t access) {
    return access_range(vm, address, NULL, size, access, 1);
}

int memory_read(VM *vm, uint32_t address, void *buffer, uint32_t size) {
    return access_range(vm, address, buffer, size, PROT_READ, 1);
}

int memory_write(VM *vm, uint32_t address, const void *buffer, uint32_t size) {
    return access_range(vm, address, (uint8_t *)buffer, size, PROT_WRITE, 1);
}

int memory_fetch(VM *vm, uint32_t address, uint32_t *word) {
    const uint8_t *memory = direct(vm, address, 4);
    if (memory) {
        *word = read_le32(memory);
        return VM_ERROR_NONE;
    }
    uint8_t bytes[4];
    int result = access_range(vm, address, bytes, 4, PROT_EXEC, 1);
    *word = result == VM_ERROR_NONE ? read_le32(bytes) : 0;
    return result;
}

// Reads for tools without raising faults, as supervisor mode sees memory; returns the number of
// bytes that could be read from the start
uint32_t memory_peek(const VM *vm, uint32_t address, void *buffer, uint32_t size) {
    uint8_t *out = buffer;
    uint32_t done = 0;

    while (done < size) {
        uint32_t virtual = address + done, physical = virtual, detail;
        uint32_t chunk = VM_PAGE_SIZE - (virtual & (VM_PAGE_SIZE - 1));
        if (chunk > size - done) {
            chunk = size - done;
        }
        if ((done > 0 && virtual == 0) ||
            (vm->control[CR_PTB] && lookup(vm, virtual, PROT_READ, 0, &physical, &detail) != LOOKUP_OK)) {
            break;
        }
        if ((uint64_t)physical + chunk > vm->memory_size) {
            chunk = physical < vm->memory_size ? vm->memory_size - physical : 0;
            memcpy(out + done, vm->memory + physical, chunk);
            return done + chunk;
        }
        memcpy(out + done, vm->memory + physical, chunk);
        done += chunk;
    }
    return done;
}

uint8_t memory_read_byte(VM *vm, uint32_t address) {
    uint8_t value = 0;
    memory_read(vm, address, &value, 1);
    return value;
}

void memory_write_byte(VM *vm, uint32_t address, uint8_t value) {
    memory_write(vm, address, &value, 1);
}

uint16_t memory_read_word(VM *vm, uint32_t address) {
    uint8_t bytes[2];
    return memory_read(vm, address, bytes, 2) == VM_ERROR_NONE ? read_le16(bytes) : 0;
}

void memory_write_word(VM *vm, uint32_t address, uint16_t value) {
    uint8_t bytes[2];
    write_le16(bytes, value);
    memory_write(vm, address, bytes, 2);
}

uint32_t memory_read_dword(VM *vm, uint32_t address) {
    const uint8_t *memory = direct(vm, address, 4);
    if (memory) {
        return read_le32(memory);
    }
    uint8_t bytes[4];
    return memory_read(vm, address, bytes, 4) == VM_ERROR_NONE ? read_le32(bytes) : 0;
}

void memory_write_dword(VM *vm, uint32_t address, uint32_t value) {
    uint8_t *memory = direct(vm, address, 4);
    if (memory) {
        write_le32(memory, value);
        return;
    }
    uint8_t bytes[4];
    write_le32(bytes, value);
    memory_write(vm, address, bytes, 4);
}

// Copies with memmove semantics so overlapping ranges are safe
int memory_copy(VM *vm, uint32_t dest, uint32_t src, uint32_t size) {
    if (memory_check_range(vm, src, size, PROT_READ) != VM_ERROR_NONE ||
        memory_check_range(vm, dest, size, PROT_WRITE) != VM_ERROR_NONE) {
        return vm->last_error;
    }
    if (!vm->control[CR_PTB]) {
        memmove(vm->memory + dest, vm->memory + src, size);
        return VM_ERROR_NONE;
    }

    uint8_t *buffer = malloc(size ? size : 1);
    if (!buffer) {
        return vm_raise(vm, VM_ERROR_MEMORY_ALLOCATION, "Out of host memory copying %u bytes", size);
    }
    if (memory_read(vm, src, buffer, size) == VM_ERROR_NONE) {
        memory_write(vm, dest, buffer, size);
    }
    free(buffer);
    return vm->last_error;
}

int memory_set(VM *vm, uint32_t address, uint8_t value, uint32_t size) {
    if (memory_check_range(vm, address, size, PROT_WRITE) != VM_ERROR_NONE) {
        return vm->last_error;
    }
    if (!vm->control[CR_PTB]) {
        memset(vm->memory + address, value, size);
        return VM_ERROR_NONE;
    }

    uint8_t chunk[VM_PAGE_SIZE];
    memset(chunk, value, sizeof(chunk));
    for (uint32_t done = 0; done < size && vm->last_error == VM_ERROR_NONE;) {
        uint32_t count = size - done < sizeof(chunk) ? size - done : (uint32_t)sizeof(chunk);
        memory_write(vm, address + done, chunk, count);
        done += count;
    }
    return vm->last_error;
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
    if (access_range(vm, (uint32_t)candidate, NULL, (uint32_t)needed, PROT_WRITE, 0) != VM_ERROR_NONE) {
        return 0;
    }
    if (!insert_block(vm, (uint32_t)candidate, (uint32_t)needed)) {
        vm_raise(vm, VM_ERROR_MEMORY_ALLOCATION, "Out of host memory for heap blocks");
        return 0;
    }

    memory_set(vm, (uint32_t)candidate, 0, (uint32_t)needed);
    return (uint32_t)candidate;
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
