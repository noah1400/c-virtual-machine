#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm.h"
#include "cpu.h"
#include "memory.h"
#include "debug.h"

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

    vm->last_error = 0;
    vm->debug_info = NULL;
    
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
    
    // Record the current PC (before execution)
    uint16_t current_pc = vm->registers[R3_PC];
    vm->error_pc = current_pc;
    
    // Fetch and decode instruction
    Instruction instr;
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
    
    // Check if this is our new format with the magic number "VM32"
    if (size >= 12 && program[0] == 'V' && program[1] == 'M' && 
        program[2] == '3' && program[3] == '2') {
        
        // Parse new format
        uint16_t version_major = *((uint16_t*)(program + 4));
        uint16_t version_minor = *((uint16_t*)(program + 6));
        uint32_t header_size = *((uint32_t*)(program + 8));
        
        // Basic validation
        if (header_size > size) {
            vm->last_error = VM_ERROR_INVALID_ADDRESS;
            snprintf(vm->error_message, sizeof(vm->error_message), 
                    "Invalid header size in program file");
            return VM_ERROR_INVALID_ADDRESS;
        }
        
        // Parse segment information
        uint32_t code_base = *((uint32_t*)(program + 12));
        uint32_t code_size = *((uint32_t*)(program + 16));
        uint32_t data_base = *((uint32_t*)(program + 20));
        uint32_t data_size = *((uint32_t*)(program + 24));
        uint32_t symbol_size = *((uint32_t*)(program + 28));
        
        // Validate sizes
        if (header_size + code_size + data_size + symbol_size > size) {
            vm->last_error = VM_ERROR_INVALID_ADDRESS;
            snprintf(vm->error_message, sizeof(vm->error_message), 
                    "Invalid segment sizes in program file");
            return VM_ERROR_INVALID_ADDRESS;
        }
        
        // Load code segment
        if (code_size > 0) {
            if (code_size > CODE_SEGMENT_SIZE) {
                vm->last_error = VM_ERROR_SEGMENTATION_FAULT;
                snprintf(vm->error_message, sizeof(vm->error_message), 
                        "Code segment too large for VM memory");
                return VM_ERROR_SEGMENTATION_FAULT;
            }
            
            memcpy(vm->memory + code_base, program + header_size, code_size);
        }
        
        // Load data segment
        if (data_size > 0) {
            if (data_size > DATA_SEGMENT_SIZE) {
                vm->last_error = VM_ERROR_SEGMENTATION_FAULT;
                snprintf(vm->error_message, sizeof(vm->error_message), 
                        "Data segment too large for VM memory");
                return VM_ERROR_SEGMENTATION_FAULT;
            }
            
            memcpy(vm->memory + data_base, 
                   program + header_size + code_size, 
                   data_size);
        }
        
        // Load debug symbols if available
        if (symbol_size > 0 && vm->debug_mode) {
            load_debug_symbols(vm, 
                              program + header_size + code_size + data_size, 
                              symbol_size);
        }
        
        // Set PC to start of code segment
        vm->registers[R3_PC] = code_base;
        
        return VM_ERROR_NONE;
    }
    
    // Fall back to the original format (backward compatibility)
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
    
    // Read the first 32 bytes to check format and get header info
    uint8_t header_buffer[32];
    size_t header_read = fread(header_buffer, 1, 32, file);
    
    // Go back to beginning of file
    fseek(file, 0, SEEK_SET);
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Check for new format with magic "VM32"
    if (header_read >= 12 && 
        header_buffer[0] == 'V' && header_buffer[1] == 'M' && 
        header_buffer[2] == '3' && header_buffer[3] == '2') {
        
        // Parse header info
        uint16_t major_ver = *((uint16_t*)(header_buffer + 4));
        uint16_t minor_ver = *((uint16_t*)(header_buffer + 6));
        uint32_t header_size = *((uint32_t*)(header_buffer + 8));
        uint32_t code_base = *((uint32_t*)(header_buffer + 12));
        uint32_t code_size = *((uint32_t*)(header_buffer + 16));
        uint32_t data_base = *((uint32_t*)(header_buffer + 20));
        uint32_t data_size = *((uint32_t*)(header_buffer + 24));
        uint32_t symbol_size = *((uint32_t*)(header_buffer + 28));
        
        printf("Loading optimized format binary (v%d.%d)\n", major_ver, minor_ver);
        printf("  Code segment: 0x%04X - %d bytes\n", code_base, code_size);
        printf("  Data segment: 0x%04X - %d bytes\n", data_base, data_size);
        
        // Validate sizes
        if (code_size > CODE_SEGMENT_SIZE) {
            fclose(file);
            vm->last_error = VM_ERROR_SEGMENTATION_FAULT;
            snprintf(vm->error_message, sizeof(vm->error_message), 
                    "Code segment too large: %d bytes (max: %d bytes)",
                    code_size, CODE_SEGMENT_SIZE);
            return VM_ERROR_SEGMENTATION_FAULT;
        }
        
        if (data_size > DATA_SEGMENT_SIZE) {
            fclose(file);
            vm->last_error = VM_ERROR_SEGMENTATION_FAULT;
            snprintf(vm->error_message, sizeof(vm->error_message), 
                    "Data segment too large: %d bytes (max: %d bytes)",
                    data_size, DATA_SEGMENT_SIZE);
            return VM_ERROR_SEGMENTATION_FAULT;
        }
        
        // Calculate file offsets for segments
        long code_offset = header_size;
        long data_offset = code_offset + code_size;
        long symbol_offset = data_offset + data_size;
        
        // Load code segment
        if (code_size > 0) {
            fseek(file, code_offset, SEEK_SET);
            size_t bytes_read = fread(vm->memory + code_base, 1, code_size, file);
            if (bytes_read != code_size) {
                fclose(file);
                vm->last_error = VM_ERROR_IO_ERROR;
                snprintf(vm->error_message, sizeof(vm->error_message), 
                        "Failed to read code segment: %zu of %d bytes",
                        bytes_read, code_size);
                return VM_ERROR_IO_ERROR;
            }
        }
        
        // Load data segment
        if (data_size > 0) {
            fseek(file, data_offset, SEEK_SET);
            size_t bytes_read = fread(vm->memory + data_base, 1, data_size, file);
            if (bytes_read != data_size) {
                fclose(file);
                vm->last_error = VM_ERROR_IO_ERROR;
                snprintf(vm->error_message, sizeof(vm->error_message), 
                        "Failed to read data segment: %zu of %d bytes",
                        bytes_read, data_size);
                return VM_ERROR_IO_ERROR;
            }
        }
        
        // Load debug symbols if present and debug mode is enabled
        if (symbol_size > 0 && vm->debug_mode) {
            printf("  Symbol table: %d bytes\n", symbol_size);
            
            // Allocate a buffer for the symbol table
            uint8_t *symbol_buffer = (uint8_t *)malloc(symbol_size);
            if (!symbol_buffer) {
                // Non-fatal error - we can continue without debug info
                printf("Warning: Failed to allocate memory for symbol table\n");
            } else {
                // Read symbol table
                fseek(file, symbol_offset, SEEK_SET);
                size_t bytes_read = fread(symbol_buffer, 1, symbol_size, file);
                if (bytes_read != symbol_size) {
                    printf("Warning: Failed to read symbol table: %zu of %d bytes\n",
                           bytes_read, symbol_size);
                    free(symbol_buffer);
                } else {
                    // Parse debug symbols
                    if (vm->debug_info) {
                        // Clean up old debug info if it exists
                        free_debug_info(vm);
                    }
                    
                    // Load new debug info
                    load_debug_symbols(vm, symbol_buffer, symbol_size);
                    free(symbol_buffer);
                }
            }
        }
        
        // Set PC to start of code segment
        vm->registers[R3_PC] = code_base;
        fclose(file);
        return VM_ERROR_NONE;
    }
    
    // Legacy format - load differently
    printf("Loading legacy format binary\n");
    
    // Check if file fits in memory
    if (file_size > vm->memory_size) {
        fclose(file);
        vm->last_error = VM_ERROR_MEMORY_ALLOCATION;
        snprintf(vm->error_message, sizeof(vm->error_message), 
                 "Program file too large: %ld bytes (memory size: %d bytes)",
                 file_size, vm->memory_size);
        return VM_ERROR_MEMORY_ALLOCATION;
    }
    
    // For legacy format, just load file directly if it fits
    if (file_size <= CODE_SEGMENT_SIZE) {
        size_t bytes_read = fread(vm->memory + CODE_SEGMENT_BASE, 1, file_size, file);
        fclose(file);
        
        if (bytes_read != file_size) {
            vm->last_error = VM_ERROR_IO_ERROR;
            snprintf(vm->error_message, sizeof(vm->error_message), 
                    "Failed to read program file: %s (read %zu of %ld bytes)",
                    filename, bytes_read, file_size);
            return VM_ERROR_IO_ERROR;
        }
        
        // Set PC to start of code segment
        vm->registers[R3_PC] = CODE_SEGMENT_BASE;
        return VM_ERROR_NONE;
    }
    else if (file_size <= vm->memory_size) {
        // Check if this might be a larger binary with data segment
        printf("  Loading large binary with possible data segment\n");
        
        // Try to find where code ends and data begins
        long data_start = CODE_SEGMENT_SIZE;
        
        // Read code segment
        size_t code_read = fread(vm->memory + CODE_SEGMENT_BASE, 1, CODE_SEGMENT_SIZE, file);
        if (code_read != CODE_SEGMENT_SIZE) {
            fclose(file);
            vm->last_error = VM_ERROR_IO_ERROR;
            snprintf(vm->error_message, sizeof(vm->error_message), 
                    "Failed to read code segment: %zu of %d bytes",
                    code_read, CODE_SEGMENT_SIZE);
            return VM_ERROR_IO_ERROR;
        }
        
        // Read data segment (as much as fits)
        long remaining = file_size - CODE_SEGMENT_SIZE;
        long data_to_read = (remaining <= DATA_SEGMENT_SIZE) ? remaining : DATA_SEGMENT_SIZE;
        
        if (data_to_read > 0) {
            size_t data_read = fread(vm->memory + DATA_SEGMENT_BASE, 1, data_to_read, file);
            if (data_read != data_to_read) {
                // Warning but not a fatal error - we loaded the code
                printf("Warning: Failed to read complete data segment: %zu of %ld bytes\n",
                       data_read, data_to_read);
            }
        }
        
        fclose(file);
        vm->registers[R3_PC] = CODE_SEGMENT_BASE;
        return VM_ERROR_NONE;
    }
    
    // File is too large for memory
    fclose(file);
    vm->last_error = VM_ERROR_SEGMENTATION_FAULT;
    snprintf(vm->error_message, sizeof(vm->error_message), 
            "Program size exceeds VM memory: %ld bytes (memory: %d bytes)",
            file_size, vm->memory_size);
    return VM_ERROR_SEGMENTATION_FAULT;
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