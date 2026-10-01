#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "binfmt.h"
#include "cpu.h"
#include "memory.h"
#include "vm.h"

// Heap blocks are tracked outside VM memory, in two treaps ordered by address. One holds the allocated
// blocks, each with the room that first fit sees after it and the most room anywhere below it, so the
// lowest gap that fits is found without visiting every block. The other keeps freed blocks until their
// space is reused, so that a second free can be told apart from a bad pointer. A guard gap that no
// access may touch precedes every block. Blocks and gaps come in units of HEAP_UNIT bytes, whose rights
// are also kept in a byte each, so that accesses can be checked without searching.
#define HEAP_GUARD      HEAP_UNIT
#define HEAP_ALIGNMENT  HEAP_UNIT

struct HeapNode {
    uint32_t start;
    uint32_t size;
    int64_t room;           // for a block between this one and the next, or the end of the heap
    int64_t most_room;      // the largest room in this subtree
    uint32_t priority;
    uint8_t protection;
    struct HeapNode *left;
    struct HeapNode *right;
};

// Nodes come in chunks, which the heap frees all at once when it is reset
#define HEAP_CHUNK_NODES 1024

struct HeapChunk {
    struct HeapChunk *next;
    uint32_t used;
    struct HeapNode nodes[HEAP_CHUNK_NODES];
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
    cpu_forget_all(vm);
    free(vm->memory);
    vm->memory = NULL;
    vm->memory_size = 0;
    memory_heap_reset(vm);
}

static struct HeapNode *new_node(VM *vm) {
    struct HeapNode *node = vm->heap_spare;
    if (node) {
        vm->heap_spare = node->left;
        return node;
    }
    struct HeapChunk *chunk = vm->heap_chunks;
    if (!chunk || chunk->used == HEAP_CHUNK_NODES) {
        chunk = malloc(sizeof(*chunk));
        if (!chunk) {
            return NULL;
        }
        chunk->next = vm->heap_chunks;
        chunk->used = 0;
        vm->heap_chunks = chunk;
    }
    return &chunk->nodes[chunk->used++];
}

// Keeps the nodes of a treap for reuse
static void release_nodes(VM *vm, struct HeapNode *node) {
    if (node) {
        release_nodes(vm, node->left);
        release_nodes(vm, node->right);
        node->left = vm->heap_spare;
        vm->heap_spare = node;
    }
}

void memory_heap_reset(VM *vm) {
    while (vm->heap_chunks) {
        struct HeapChunk *next = vm->heap_chunks->next;
        free(vm->heap_chunks);
        vm->heap_chunks = next;
    }
    vm->heap_spare = NULL;
    vm->heap_blocks = NULL;
    vm->heap_freed = NULL;
    vm->heap_last = NULL;
    free(vm->heap_rights);
    vm->heap_rights = NULL;
    vm->heap_rights_base = vm->control[CR_HEAPLO] & ~(HEAP_UNIT - 1);
    vm->heap_rights_count = 0;
}

// Records the rights of a block for its units, 0 once it is freed. The record grows to reach them; units that
// it cannot reach for lack of host memory are checked by searching the blocks instead.
static void set_rights(VM *vm, uint32_t start, uint32_t size, uint8_t rights) {
    uint32_t first = (start - vm->heap_rights_base) / HEAP_UNIT, units = size / HEAP_UNIT;
    if (first + units > vm->heap_rights_count) {
        uint32_t count = vm->heap_rights_count ? vm->heap_rights_count : 4096;
        while (count < first + units) {
            count *= 2;
        }
        uint8_t *grown = realloc(vm->heap_rights, count);
        if (grown) {
            memset(grown + vm->heap_rights_count, 0, count - vm->heap_rights_count);
            vm->heap_rights = grown;
            vm->heap_rights_count = count;
        }
    }
    if (first < vm->heap_rights_count) {
        uint32_t reached = vm->heap_rights_count - first;
        memset(vm->heap_rights + first, rights, units < reached ? units : reached);
    }
}

static inline int in_heap(const VM *vm, uint32_t address, uint32_t size) {
    return (uint64_t)address + size > vm->control[CR_HEAPLO] && address < vm->control[CR_HEAPHI];
}

static int64_t most_room(const struct HeapNode *node) {
    return node ? node->most_room : INT64_MIN;
}

static void update(struct HeapNode *node) {
    node->most_room = node->room;
    if (most_room(node->left) > node->most_room) {
        node->most_room = node->left->most_room;
    }
    if (most_room(node->right) > node->most_room) {
        node->most_room = node->right->most_room;
    }
}

// Splits a treap into the nodes that start before key and the others
static void split(struct HeapNode *node, uint32_t key, struct HeapNode **before, struct HeapNode **others) {
    if (!node) {
        *before = *others = NULL;
    } else if (node->start < key) {
        *before = node;
        split(node->right, key, &node->right, others);
        update(node);
    } else {
        *others = node;
        split(node->left, key, before, &node->left);
        update(node);
    }
}

// Joins two treaps whose nodes all start in order
static struct HeapNode *merge(struct HeapNode *a, struct HeapNode *b) {
    if (!a || !b) {
        return a ? a : b;
    }
    if (a->priority > b->priority) {
        a->right = merge(a->right, b);
        update(a);
        return a;
    }
    b->left = merge(a, b->left);
    update(b);
    return b;
}

static struct HeapNode *first_node(struct HeapNode *node) {
    while (node && node->left) {
        node = node->left;
    }
    return node;
}

static struct HeapNode *last_node(struct HeapNode *node) {
    while (node && node->right) {
        node = node->right;
    }
    return node;
}

// The node that starts last at or before address, or NULL
static struct HeapNode *node_before(struct HeapNode *node, uint32_t address) {
    struct HeapNode *found = NULL;
    while (node) {
        if (node->start <= address) {
            found = node;
            node = node->right;
        } else {
            node = node->left;
        }
    }
    return found;
}

// The room for a block after end and its guard, before the guard of next or the end of the heap
static int64_t room_after(const VM *vm, uint64_t end, const struct HeapNode *next) {
    int64_t candidate = (int64_t)end + HEAP_GUARD;
    return next ? (int64_t)next->start - HEAP_GUARD - candidate : (int64_t)vm->control[CR_HEAPHI] - candidate;
}

// Gives the last node of a treap its room before next, which follows the treap
static void set_last_room(const VM *vm, struct HeapNode *node, const struct HeapNode *next) {
    if (node->right) {
        set_last_room(vm, node->right, next);
    } else {
        node->room = room_after(vm, (uint64_t)node->start + node->size, next);
    }
    update(node);
}

static uint32_t next_priority(VM *vm) {
    uint32_t x = vm->heap_seed ? vm->heap_seed : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return vm->heap_seed = x;
}

// The allocated block whose data contains address, or NULL; heap accesses mostly stay in one block
static struct HeapNode *find_block(VM *vm, uint32_t address) {
    struct HeapNode *block = vm->heap_last;
    if (!block || address - block->start >= block->size) {
        block = node_before(vm->heap_blocks, address);
        if (!block || address - block->start >= block->size) {
            return NULL;
        }
        vm->heap_last = block;
    }
    return block;
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

    struct HeapNode *block = find_block(vm, address);
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
        if (buffer && access == PROT_WRITE) {
            cpu_forget(vm, address, size);
        }
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
        if (buffer && access == PROT_WRITE) {
            cpu_forget(vm, physical, chunk);
        }
        done += chunk;
    }
    return VM_ERROR_NONE;
}

// The host address of an access that needs no translation and that the heap rules allow, or NULL when
// memory_read or memory_write would translate or fault
uint8_t *memory_direct(VM *vm, uint32_t address, uint32_t size, uint8_t access) {
    if (vm->control[CR_PTB] || (uint64_t)address + size > vm->memory_size) {
        return NULL;
    }
    if (in_heap(vm, address, size)) {
        if (vm->heap_rights && size <= HEAP_UNIT) {
            return memory_heap_allows(vm, address, size, access) ? vm->memory + address : NULL;
        }
        const struct HeapNode *block = find_block(vm, address);
        if (!block || size > block->start + block->size - address || (block->protection & access) != access) {
            return NULL;
        }
    }
    return vm->memory + address;
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
        cpu_forget(vm, address, 4);
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
        cpu_forget(vm, dest, size);
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
        cpu_forget(vm, address, size);
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

// Forgets the freed blocks that overlap the addresses from low up to high
static void forget_freed(VM *vm, uint32_t low, uint32_t high) {
    struct HeapNode *before, *others, *inside, *after;
    split(vm->heap_freed, low, &before, &others);
    split(others, high, &inside, &after);
    release_nodes(vm, inside);
    // Freed blocks do not overlap, so only the last one before low can reach into the range
    struct HeapNode *last = last_node(before);
    if (last && (uint64_t)last->start + last->size > low) {
        split(before, last->start, &before, &inside);
        release_nodes(vm, inside);
    }
    vm->heap_freed = merge(before, after);
}

// Adds an allocated block, forgetting freed blocks whose space it reuses
static int insert_block(VM *vm, uint32_t start, uint32_t size) {
    struct HeapNode *block = new_node(vm);
    if (!block) {
        return 0;
    }
    forget_freed(vm, start - HEAP_GUARD, start + size);

    struct HeapNode *before, *after;
    split(vm->heap_blocks, start, &before, &after);
    *block = (struct HeapNode){ .start = start, .size = size, .priority = next_priority(vm), .protection = PROT_ALL };
    block->room = room_after(vm, (uint64_t)start + size, first_node(after));
    update(block);
    if (before) {
        set_last_room(vm, before, block);
    }
    vm->heap_blocks = merge(merge(before, block), after);
    vm->heap_last = block;
    set_rights(vm, start, size, HEAP_UNIT_USED | PROT_ALL);
    return 1;
}

// The first block in address order with room for needed bytes after it; one of them has it
static struct HeapNode *first_fit(struct HeapNode *node, int64_t needed) {
    for (;;) {
        if (most_room(node->left) >= needed) {
            node = node->left;
        } else if (node->room >= needed) {
            return node;
        } else {
            node = node->right;
        }
    }
}

// Allocate zero-filled memory from the heap, returning 0 on failure
uint32_t memory_allocate(VM *vm, uint32_t size) {
    uint32_t low = vm->control[CR_HEAPLO], high = vm->control[CR_HEAPHI];
    uint64_t needed = size < HEAP_ALIGNMENT ? HEAP_ALIGNMENT : ((uint64_t)size + HEAP_ALIGNMENT - 1) & ~(uint64_t)(HEAP_ALIGNMENT - 1);

    if (high <= low || needed + HEAP_GUARD > high - low) {
        vm_raise(vm, VM_ERROR_MEMORY_ALLOCATION, "Allocation size too large: %u bytes", size);
        return 0;
    }

    // First fit: before the first block, or in the lowest gap after a block that has room
    uint64_t base = ((uint64_t)low + HEAP_ALIGNMENT - 1) & ~(uint64_t)(HEAP_ALIGNMENT - 1);
    uint64_t candidate;
    if (room_after(vm, base, first_node(vm->heap_blocks)) >= (int64_t)needed) {
        candidate = base + HEAP_GUARD;
    } else if (most_room(vm->heap_blocks) >= (int64_t)needed) {
        struct HeapNode *block = first_fit(vm->heap_blocks, (int64_t)needed);
        candidate = (uint64_t)block->start + block->size + HEAP_GUARD;
    } else {
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

// Counts the gaps before the blocks of a treap in address order; gap is where the next one starts
static void count_gaps(const struct HeapNode *node, uint64_t *gap, uint32_t *free_bytes, uint32_t *largest_free) {
    if (node) {
        count_gaps(node->left, gap, free_bytes, largest_free);
        count_gap(*gap, (uint64_t)node->start - HEAP_GUARD, free_bytes, largest_free);
        *gap = (uint64_t)node->start + node->size;
        count_gaps(node->right, gap, free_bytes, largest_free);
    }
}

// Total and largest free payload bytes in the heap
void memory_heap_stats(const VM *vm, uint32_t *free_bytes, uint32_t *largest_free) {
    uint64_t gap = vm->control[CR_HEAPLO];

    *free_bytes = 0;
    *largest_free = 0;
    count_gaps(vm->heap_blocks, &gap, free_bytes, largest_free);
    count_gap(gap, vm->control[CR_HEAPHI], free_bytes, largest_free);
}

// Free an allocated block given the address returned by memory_allocate
int memory_free(VM *vm, uint32_t address) {
    if (!in_heap(vm, address, 1)) {
        return vm_raise(vm, VM_ERROR_INVALID_ADDRESS, "Invalid heap address for free: 0x%04X", address);
    }
    struct HeapNode *block = node_before(vm->heap_blocks, address);
    if (!block || block->start != address) {
        struct HeapNode *freed = node_before(vm->heap_freed, address);
        if (freed && freed->start == address) {
            return vm_raise(vm, VM_ERROR_INVALID_ADDRESS, "Double free detected at 0x%04X", address);
        }
        return vm_raise(vm, VM_ERROR_INVALID_ADDRESS,
                        "Address 0x%04X is not the start of an allocated block", address);
    }

    struct HeapNode *before, *others, *after;
    split(vm->heap_blocks, address, &before, &others);
    split(others, address + 1, &others, &after);
    if (before) {
        set_last_room(vm, before, first_node(after));
    }
    vm->heap_blocks = merge(before, after);
    if (vm->heap_last == block) {
        vm->heap_last = NULL;
    }

    set_rights(vm, block->start, block->size, 0);
    block->left = block->right = NULL;
    update(block);
    split(vm->heap_freed, address, &before, &after);
    vm->heap_freed = merge(merge(before, block), after);
    return VM_ERROR_NONE;
}

// Set the protection flags of an allocated block
int memory_protect(VM *vm, uint32_t address, uint8_t flags) {
    struct HeapNode *block = node_before(vm->heap_blocks, address);
    if (!block || block->start != address) {
        return vm_raise(vm, VM_ERROR_INVALID_ADDRESS,
                        "Address 0x%04X is not the start of an allocated block", address);
    }

    block->protection = flags & PROT_ALL;
    set_rights(vm, block->start, block->size, (uint8_t)(HEAP_UNIT_USED | block->protection));
    return VM_ERROR_NONE;
}
