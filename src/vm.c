#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm.h"
#include "cpu.h"
#include "memory.h"
#include "debug.h"
#include "binfmt.h"

// Initialize the VM with the specified memory size
int vm_init(VM *vm, uint32_t memory_size) {
    if (!vm) {
        return VM_ERROR_INVALID_ADDRESS;
    }

    memset(vm, 0, sizeof(*vm));

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
    vm->rng_state = VM_RNG_DEFAULT_SEED;
    
    return VM_ERROR_NONE;
}

// Clean up VM resources
void vm_cleanup(VM *vm) {
    if (!vm) {
        return;
    }
    
    // Free memory
    memory_cleanup(vm);
    debug_info_free(vm->debug_info);
    vm->debug_info = NULL;
    
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

    // A faulted VM stays stopped until it is reset
    if (vm->last_error != VM_ERROR_NONE) {
        return vm->last_error;
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

// Decode the 32-bit instruction at the specified memory address
int vm_decode_instruction(VM *vm, uint16_t address, Instruction *instr) {
    if (!vm || !instr) {
        return VM_ERROR_INVALID_ADDRESS;
    }

    if (memory_check_address(vm, address, 4) != VM_ERROR_NONE) {
        return VM_ERROR_SEGMENTATION_FAULT;
    }

    isa_decode(memory_read_dword(vm, address), instr);
    return VM_ERROR_NONE;
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

static int segment_fits(uint32_t base, uint32_t size, uint32_t seg_base, uint32_t seg_size) {
    return base >= seg_base && (uint64_t)base + size <= (uint64_t)seg_base + seg_size;
}

static int load_vm32_image(VM *vm, const uint8_t *image, uint32_t size) {
    Vm32Image bin;
    const char *problem = vm32_parse(image, size, &bin);
    if (problem) {
        return vm_raise(vm, VM_ERROR_IO_ERROR, "%s", problem);
    }

    uint32_t code_base = bin.code_base, code_size = bin.code_size;
    uint32_t data_base = bin.data_base, data_size = bin.data_size;

    if (!segment_fits(code_base, code_size, CODE_SEGMENT_BASE, CODE_SEGMENT_SIZE)) {
        return vm_raise(vm, VM_ERROR_SEGMENTATION_FAULT, "Code segment does not fit the code segment range");
    }
    if (!segment_fits(data_base, data_size, DATA_SEGMENT_BASE, DATA_SEGMENT_SIZE)) {
        return vm_raise(vm, VM_ERROR_SEGMENTATION_FAULT, "Data segment does not fit the data segment range");
    }

    printf("Loading optimized format binary (v%d.%d)\n", bin.version_major, bin.version_minor);
    printf("  Code segment: 0x%04X - %u bytes\n", code_base, code_size);
    printf("  Data segment: 0x%04X - %u bytes\n", data_base, data_size);

    memcpy(vm->memory + code_base, bin.code, code_size);
    memcpy(vm->memory + data_base, bin.data, data_size);

    if (bin.symbol_size > 0 && vm->debug_mode) {
        debug_info_free(vm->debug_info);
        vm->debug_info = debug_info_parse(bin.symbols, bin.symbol_size);
    }

    vm->registers[R3_PC] = code_base;
    return VM_ERROR_NONE;
}

// Raw images without a header are loaded contiguously from address 0
static int load_raw_image(VM *vm, const uint8_t *image, uint32_t size) {
    if (size > CODE_SEGMENT_SIZE + DATA_SEGMENT_SIZE) {
        return vm_raise(vm, VM_ERROR_SEGMENTATION_FAULT, "Raw program image exceeds code and data segments");
    }

    printf("Loading legacy format binary\n");
    memcpy(vm->memory + CODE_SEGMENT_BASE, image, size);
    vm->registers[R3_PC] = CODE_SEGMENT_BASE;
    return VM_ERROR_NONE;
}

// Load a program image from memory
int vm_load_program(VM *vm, const uint8_t *program, uint32_t size) {
    if (!vm || !program) {
        return VM_ERROR_INVALID_ADDRESS;
    }

    if (vm32_is_image(program, size)) {
        return load_vm32_image(vm, program, size);
    }
    return load_raw_image(vm, program, size);
}

// Load a program from a file
int vm_load_program_file(VM *vm, const char *filename) {
    if (!vm || !filename) {
        return VM_ERROR_INVALID_ADDRESS;
    }

    uint32_t size;
    const char *problem;
    uint8_t *buffer = read_binary_file(filename, &size, &problem);
    if (!buffer) {
        return vm_raise(vm, VM_ERROR_IO_ERROR, "%s: %s", problem, filename);
    }

    int result = vm_load_program(vm, buffer, size);
    free(buffer);
    return result;
}

// Records a fault; the first fault since the error state was last cleared is kept
int vm_raise(VM *vm, int code, const char *format, ...) {
    if (vm->last_error == VM_ERROR_NONE) {
        va_list args;
        va_start(args, format);
        vsnprintf(vm->error_message, sizeof(vm->error_message), format, args);
        va_end(args);
        vm->last_error = code;
    }
    return code;
}

void vm_clear_error(VM *vm) {
    vm->last_error = VM_ERROR_NONE;
    vm->error_message[0] = '\0';
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