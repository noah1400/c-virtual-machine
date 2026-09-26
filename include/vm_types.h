#ifndef _VM_TYPES_H_
#define _VM_TYPES_H_

#include <stdint.h>
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

// Memory segment base addresses
#define CODE_SEGMENT_BASE   0x0000
#define CODE_SEGMENT_SIZE   0x4000
#define DATA_SEGMENT_BASE   0x4000
#define DATA_SEGMENT_SIZE   0x4000
#define STACK_SEGMENT_BASE  0x8000
#define STACK_SEGMENT_SIZE  0x4000
#define HEAP_SEGMENT_BASE   0xC000
#define HEAP_SEGMENT_SIZE   0x4000
#define VM_ADDRESS_SPACE_SIZE 0x10000u

// Interrupt vector table: 256 handler addresses of 4 bytes each
#define INTERRUPT_VECTOR_TABLE 0x0100

#define VM_RNG_DEFAULT_SEED     0x12345678


// Virtual Machine state
typedef struct {
    // CPU registers
    uint32_t registers[16];  // R0-R15
    
    // Memory
    uint8_t *memory;         // Main memory array
    uint32_t memory_size;    // Total size of memory
    
    // VM state flags
    uint8_t halted;          // VM halted flag
    uint8_t debug_mode;      // Debug mode flag
    
    struct IODevices *io_devices;
    
    // Interrupt state
    
    // Instruction cycle info for debugging
    uint32_t instruction_count; // Number of instructions executed
    Instruction current_instr;  // Currently executing instruction
    uint32_t error_pc;         // Address of last error
    
    // Error handling
    int last_error;          // Last error code
    char error_message[256]; // Error message

    uint32_t rng_state;      // Random number generator state
    uint32_t exit_code;      // Set by the exit syscall
    uint64_t start_ms;       // Host clock when the VM started

    struct DebugInfo *debug_info;  // Debug information (NULL if not loaded)
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
#define VM_ERROR_NESTED_INTERRUPT     13 // Nested interrupt

#endif // _VM_TYPES_H_