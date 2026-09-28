#ifndef _VM_TYPES_H_
#define _VM_TYPES_H_

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "instruction_set.h"

// Register definitions
#define R0_ACC  0  // Accumulator
#define R1_BP   1  // Base pointer - current stack frame base
#define R2_SP   2  // Stack pointer - points to stack top
#define R3_PC   3  // Program counter - next instruction address
#define R4_SR   4  // Status register - contains flags
#define R5      5  // General-purpose register
#define R6      6  // General-purpose register
#define R7      7  // General-purpose register
#define R8      8  // General-purpose register
#define R9      9  // General-purpose register
#define R10     10 // General-purpose register
#define R11     11 // General-purpose register
#define R12     12 // General-purpose register
#define R13     13 // General-purpose register
#define R14     14 // General-purpose register
#define R15_LR  15 // Link register - return address storage

// Code starts at 0 with data on the next page; the heap lies between the program and the stack,
// which occupies the top of memory and grows down
#define VM_PAGE_SIZE 0x1000u
#define VM_STACK_SIZE 0x10000u
#define VM_MIN_MEMORY_SIZE (2 * VM_STACK_SIZE)

#define VM_RNG_DEFAULT_SEED     0x12345678

// Files opened by programs get handles 3 and up; 0-2 are stdin, stdout and stderr
#define VM_MAX_FILES            16
#define VM_FIRST_FILE_HANDLE    3

#define VM_CALL_FRAMES          1024

// A call or interrupt that has not returned yet, kept for backtraces
typedef struct {
    uint32_t site;          // the CALL, or the instruction the interrupt came before
    uint32_t slot;          // stack address of the return address, or of the saved registers
    uint32_t resume;        // where execution returns to, which the slot has to hold still
    int32_t vector;         // the interrupt vector, or -1 for a call
} CallFrame;

// Virtual Machine state
typedef struct {
    // CPU registers
    uint32_t registers[16];  // R0-R15
    uint32_t control[CR_COUNT];

    // Memory
    uint8_t *memory;         // Main memory array
    uint32_t memory_size;    // Total size of memory
    uint32_t stack_size;     // Room for the stack at the top of memory

    // VM state flags
    uint8_t halted;          // VM halted flag
    uint8_t debug_mode;      // Debug mode flag
    uint8_t break_requested; // Set by the DEBUG instruction for the debugger
    uint32_t irq_mask[8];    // Device interrupts waiting for the interrupt flag, one bit per vector
    uint8_t irq_pending;     // Some bit of irq_mask is set
    uint8_t entered_interrupt;  // The current step entered an interrupt handler
    uint8_t exception;          // Vector of a fault the current step delivered to its handler, or 0
    char exception_message[256];

    struct IODevices *io_devices;
    uint32_t io_ticking;     // one bit for every device that counts executed instructions

    // Instruction cycle info for debugging
    uint32_t instruction_count; // Number of instructions executed
    uint32_t instruction_limit; // Execution stops with an error after this many, 0 for no limit
    uint32_t output_flushed;    // instruction count when stdout was last written out
    Instruction current_instr;  // Currently executing instruction
    uint32_t error_pc;         // Address of last error

    // Error handling
    int last_error;          // Last error code
    char error_message[256]; // Error message

    uint32_t rng_state;      // Random number generator state
    uint32_t exit_code;      // Set by the exit syscall
    uint64_t start_ms;       // Host clock when the VM started
    uint32_t entry_point;    // Where execution starts and RESET returns to
    uint32_t code_end;       // End of the loaded code
    uint32_t image_end;      // End of the loaded code and data, where the heap starts
    struct HeapNode *heap_blocks;  // allocated heap blocks, by address
    struct HeapNode *heap_freed;   // freed heap blocks whose space is not reused yet
    struct HeapNode *heap_last;    // the block of the latest heap access
    uint32_t heap_seed;            // for the priorities that shape the heap treaps
    uint8_t *heap_rights;          // for every 8 bytes of the heap up to the blocks so far, the rights of their block
    uint32_t heap_rights_base;     // the address of the first 8 bytes
    uint32_t heap_rights_count;
    FILE *files[VM_MAX_FILES];
    int arg_count;           // Program arguments, starting with the program path
    char **args;

    struct DebugInfo *debug_info;  // Debug information (NULL if not loaded)
    CallFrame call_frames[VM_CALL_FRAMES];  // shadow call stack, innermost last
    uint32_t call_depth;
} VM;

// Error codes
#define VM_ERROR_NONE                 0  // No error
#define VM_ERROR_INVALID_INSTRUCTION  1  // Invalid instruction
#define VM_ERROR_SEGMENTATION_FAULT   2  // Memory access violation
#define VM_ERROR_STACK_OVERFLOW       3  // Stack overflow
#define VM_ERROR_STACK_UNDERFLOW      4  // Stack underflow
#define VM_ERROR_DIVISION_BY_ZERO     5  // Division by zero
#define VM_ERROR_INVALID_ADDRESS      6  // Invalid memory address
#define VM_ERROR_INVALID_SYSCALL      7  // Invalid system call
#define VM_ERROR_MEMORY_ALLOCATION    8  // Memory allocation error
#define VM_ERROR_INVALID_ALIGNMENT    9  // Memory alignment error
#define VM_ERROR_UNHANDLED_INTERRUPT  10 // Unhandled interrupt
#define VM_ERROR_IO_ERROR             11 // I/O operation error
#define VM_ERROR_PROTECTION_FAULT     12 // Memory protection fault
#define VM_ERROR_PRIVILEGE            13 // Privileged instruction in user mode
#define VM_ERROR_PAGE_FAULT           14 // Page table does not allow the access
#define VM_ERROR_INSTRUCTION_LIMIT    16 // Instruction limit reached, never delivered to the program
#define VM_ERROR_SIGNAL               17 // A signal stopped the VM, never delivered to the program
#define VM_ERROR_ABORT                18 // The program stopped itself with a message, never delivered

// Faults with a code below this are delivered to the interrupt vector of the same number
#define VM_EXCEPTION_VECTORS          16

// Raised after every instruction that starts with the trap flag set
#define VM_TRAP_VECTOR                15

#endif // _VM_TYPES_H_
