#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm.h"
#include "cpu.h"
#include "memory.h"

// Initialize the VM with the specified memory size
int vm_init(VM *vm, uint32_t memory_size) {
    if (!vm) {
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    // Initialize memory subsystem
    int result = memory_init(vm, memory_size);
    if (result != VM_ERROR_NONE) {
        return result;
    }
    
    // Initialize CPU (registers and flags)
    result = cpu_init(vm);
    if (result != VM_ERROR_NONE) {
        memory_cleanup(vm);
        return result;
    }
    
    // Initialize I/O devices (if any)
    vm->io_devices = NULL;  // No I/O devices by default
    
    // Clear error state
    vm->last_error = VM_ERROR_NONE;
    memset(vm->error_message, 0, sizeof(vm->error_message));
    
    return VM_ERROR_NONE;
}

// Clean up VM resources
void vm_cleanup(VM *vm) {
    if (!vm) {
        return;
    }
    
    // Free memory
    memory_cleanup(vm);
    
    // Free I/O devices (if any)
    if (vm->io_devices) {
        free(vm->io_devices);
        vm->io_devices = NULL;
    }
}

// Reset the VM to initial state
int vm_reset(VM *vm) {
    if (!vm) {
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    // Reset CPU state
    int result = cpu_reset(vm);
    if (result != VM_ERROR_NONE) {
        return result;
    }
    
    // Clear memory (optional - this can be expensive)
    if (vm->memory) {
        memset(vm->memory, 0, vm->memory_size);
    }
    
    // Reset VM state flags
    vm->halted = 0;
    vm->debug_mode = 0;
    vm->instruction_count = 0;
    
    // Clear error state
    vm->last_error = VM_ERROR_NONE;
    memset(vm->error_message, 0, sizeof(vm->error_message));
    
    return VM_ERROR_NONE;
}

// Run the VM until halted
int vm_run(VM *vm) {
    if (!vm) {
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    // Execute instructions until halted or error
    while (!vm->halted) {
        int result = vm_step(vm);
        if (result != VM_ERROR_NONE) {
            return result;
        }
    }
    
    return VM_ERROR_NONE;
}

int vm_step(VM *vm) {
    if (!vm) {
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    // Check if VM is halted
    if (vm->halted) {
        return VM_ERROR_NONE;
    }
    
    // Fetch and decode instruction
    Instruction instr;
    uint16_t current_pc = vm->registers[R3_PC];
    
    int result = vm_decode_instruction(vm, current_pc, &instr);
    if (result != VM_ERROR_NONE) {
        return result;
    }
    
    // Save current instruction for debugging
    vm->current_instr = instr;
    
    // IMPORTANT: Increment PC BEFORE executing the instruction
    // This is because some instructions (like CALL) rely on PC pointing to the next instruction
    vm->registers[R3_PC] += 4;
    
    // Execute instruction and get result
    result = cpu_execute_instruction(vm, &instr);
    
    // Check for errors
    if (result != VM_ERROR_NONE) {
        return result;
    } else if (vm->last_error != VM_ERROR_NONE) {
        return vm->last_error;
    }
    
    // Increment instruction count
    vm->instruction_count++;
    
    return VM_ERROR_NONE;
}

// Execute the current instruction pointed to by PC
int vm_execute_instruction(VM *vm) {
    if (!vm) {
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    // Fetch and decode instruction at PC
    Instruction instr;
    int result = vm_decode_instruction(vm, vm->registers[R3_PC], &instr);
    if (result != VM_ERROR_NONE) {
        return result;
    }
    
    // Execute the instruction
    return cpu_execute_instruction(vm, &instr);
}

// Memory access wrappers
uint8_t vm_read_byte(VM *vm, uint16_t address) {
    return memory_read_byte(vm, address);
}

void vm_write_byte(VM *vm, uint16_t address, uint8_t value) {
    memory_write_byte(vm, address, value);
}

uint16_t vm_read_word(VM *vm, uint16_t address) {
    return memory_read_word(vm, address);
}

void vm_write_word(VM *vm, uint16_t address, uint16_t value) {
    memory_write_word(vm, address, value);
}

uint32_t vm_read_dword(VM *vm, uint16_t address) {
    return memory_read_dword(vm, address);
}

void vm_write_dword(VM *vm, uint16_t address, uint32_t value) {
    memory_write_dword(vm, address, value);
}

// I/O operations (placeholder implementations)
int vm_io_read(VM *vm, uint16_t port) {
    // Placeholder - implement I/O device reading
    return 0;
}

void vm_io_write(VM *vm, uint16_t port, uint32_t value) {
    // Placeholder - implement I/O device writing
    
    // Special case for console output
    if (port == 0) {
        printf("%c", (char)value);
    }
}

// Load a program into memory
int vm_load_program(VM *vm, const uint8_t *program, uint32_t size) {
    if (!vm || !program) {
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    // Check if program fits in code segment
    if (size > CODE_SEGMENT_SIZE) {
        vm->last_error = VM_ERROR_SEGMENTATION_FAULT;
        snprintf(vm->error_message, sizeof(vm->error_message), 
                 "Program size (%d bytes) exceeds code segment size (%d bytes)",
                 size, CODE_SEGMENT_SIZE);
        return VM_ERROR_SEGMENTATION_FAULT;
    }
    
    // Copy program to code segment
    memcpy(vm->memory + CODE_SEGMENT_BASE, program, size);
    
    // Reset PC to start of code segment
    vm->registers[R3_PC] = CODE_SEGMENT_BASE;
    
    return VM_ERROR_NONE;
}

// Load a program from a file
int vm_load_program_file(VM *vm, const char *filename) {
    if (!vm || !filename) {
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    FILE *file = fopen(filename, "rb");
    if (!file) {
        vm->last_error = VM_ERROR_IO_ERROR;
        snprintf(vm->error_message, sizeof(vm->error_message), 
                 "Failed to open program file: %s", filename);
        return VM_ERROR_IO_ERROR;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // // Check if program fits in code segment
    // if (file_size > CODE_SEGMENT_SIZE) {
    //     fclose(file);
    //     vm->last_error = VM_ERROR_SEGMENTATION_FAULT;
    //     snprintf(vm->error_message, sizeof(vm->error_message), 
    //              "Program size (%ld bytes) exceeds code segment size (%d bytes)",
    //              file_size, CODE_SEGMENT_SIZE);
    //     return VM_ERROR_SEGMENTATION_FAULT;
    // }
    
    // Read file into code segment
    size_t bytes_read = fread(vm->memory + CODE_SEGMENT_BASE, 1, file_size, file);
    fclose(file);
    
    if (bytes_read != file_size) {
        vm->last_error = VM_ERROR_IO_ERROR;
        snprintf(vm->error_message, sizeof(vm->error_message), 
                 "Failed to read program file: %s (read %zu of %ld bytes)",
                 filename, bytes_read, file_size);
        return VM_ERROR_IO_ERROR;
    }
    
    // Reset PC to start of code segment
    vm->registers[R3_PC] = CODE_SEGMENT_BASE;
    
    return VM_ERROR_NONE;
}

// Get error message for error code
const char* vm_get_error_string(int error_code) {
    switch (error_code) {
        case VM_ERROR_NONE:
            return "No error";
        case VM_ERROR_INVALID_INSTRUCTION:
            return "Invalid instruction";
        case VM_ERROR_SEGMENTATION_FAULT:
            return "Segmentation fault";
        case VM_ERROR_STACK_OVERFLOW:
            return "Stack overflow";
        case VM_ERROR_STACK_UNDERFLOW:
            return "Stack underflow";
        case VM_ERROR_DIVISION_BY_ZERO:
            return "Division by zero";
        case VM_ERROR_INVALID_ADDRESS:
            return "Invalid memory address";
        case VM_ERROR_INVALID_SYSCALL:
            return "Invalid system call";
        case VM_ERROR_MEMORY_ALLOCATION:
            return "Memory allocation error";
        case VM_ERROR_INVALID_ALIGNMENT:
            return "Memory alignment error";
        case VM_ERROR_UNHANDLED_INTERRUPT:
            return "Unhandled interrupt";
        case VM_ERROR_IO_ERROR:
            return "I/O operation error";
        case VM_ERROR_PROTECTION_FAULT:
            return "Memory protection fault";
        default:
            return "Unknown error";
    }
}

// Get last error code
int vm_get_last_error(VM *vm) {
    if (!vm) {
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    return vm->last_error;
}

// Get last error message
const char* vm_get_last_error_message(VM *vm) {
    if (!vm) {
        return "Invalid VM pointer";
    }
    
    return vm->error_message;
}

// Dump VM state for debugging
void vm_dump_state(VM *vm) {
    if (!vm) {
        return;
    }
    
    printf("=== VM State Dump ===\n");
    printf("Memory size: %u bytes\n", vm->memory_size);
    printf("Halted: %s\n", vm->halted ? "Yes" : "No");
    printf("Debug mode: %s\n", vm->debug_mode ? "Yes" : "No");
    printf("Instruction count: %u\n", vm->instruction_count);
    
    if (vm->last_error != VM_ERROR_NONE) {
        printf("Last error: %s (%d)\n", vm_get_error_string(vm->last_error), vm->last_error);
        printf("Error message: %s\n", vm->error_message);
    }
    
    printf("\n");
    
    // Dump CPU registers
    cpu_dump_registers(vm);
    
    printf("\n");
}

const char* vm_get_error_message(VM *vm) {
    if (!vm) {
        return "Invalid VM pointer";
    }
    
    if (vm->last_error == VM_ERROR_NONE) {
        return "No error";
    }
    
    return vm->error_message;
}