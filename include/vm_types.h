#ifndef _VM_TYPES_H_
#define _VM_TYPES_H_

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

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

// Special stack frame offsets
#define FRAME_PREV_BP_OFFSET    0
#define FRAME_RET_ADDR_OFFSET   4
#define FRAME_FIRST_LOCAL       8

// Symbol represents a labeled address in the program
typedef struct {
    char *name;          // Symbol name
    uint32_t address;    // Symbol address
    uint8_t type;        // Symbol type (0=code, 1=data)
    uint32_t line_num;   // Source line number
    char *source_file;   // Source file path
} Symbol;

// SourceLine represents a line from the source code
typedef struct {
    uint32_t address;     // Program address
    uint32_t line_num;    // Source line number
    char *source;         // Source line text
    char *source_file;    // Source file path
} SourceLine;

// DebugInfo holds all debugging information
typedef struct {
    // Symbol table
    Symbol *symbols;
    uint32_t symbol_count;
    
    // Source line information
    SourceLine *source_lines;
    uint32_t source_line_count;
} DebugInfo;

#define MAX_BREAKPOINTS 32

typedef struct {
    uint32_t address;
    char *name;          // Optional name (could be a symbol)
    bool enabled;
} Breakpoint;



// Instruction structure that represents a decoded instruction
typedef struct {
    uint8_t opcode;          // 8-bit opcode
    uint8_t mode;            // 4-bit addressing mode
    uint8_t reg1;            // 4-bit register 1
    uint8_t reg2;            // 4-bit register 2
    uint16_t immediate;      // 12-bit immediate/offset
} Instruction;

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
    
    // I/O state
    void *io_devices;        // I/O devices structure (defined in io_devices.h)
    
    // Interrupt state
    uint32_t interrupt_vector;  // Current interrupt vector
    uint8_t interrupt_enabled;  // Interrupt enable status
    
    // Instruction cycle info for debugging
    uint32_t instruction_count; // Number of instructions executed
    Instruction current_instr;  // Currently executing instruction
    uint16_t error_pc;         // Address of last error
    
    // Error handling
    int last_error;          // Last error code
    char error_message[256]; // Error message

    DebugInfo *debug_info;  // Debug information (NULL if not loaded)
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