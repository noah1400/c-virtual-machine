#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "binfmt.h"
#include "cpu.h"
#include "debug.h"
#include "io.h"
#include "memory.h"
#include "syscalls.h"
#include "vm.h"

int vm_init(VM *vm, uint32_t memory_size) {
    memset(vm, 0, sizeof(*vm));

    int result = memory_init(vm, memory_size);
    if (result != VM_ERROR_NONE) {
        return result;
    }

    cpu_reset(vm);
    syscalls_init(vm);
    vm->rng_state = VM_RNG_DEFAULT_SEED;
    return io_init(vm);
}

void vm_cleanup(VM *vm) {
    memory_cleanup(vm);
    debug_info_free(vm->debug_info);
    vm->debug_info = NULL;
    io_cleanup(vm);
    syscalls_cleanup(vm);
}

int vm_run(VM *vm) {
    while (!vm->halted) {
        int result = vm_step(vm);
        if (result != VM_ERROR_NONE) {
            return result;
        }
    }
    return VM_ERROR_NONE;
}

int vm_step(VM *vm) {
    if (vm->halted) {
        return VM_ERROR_NONE;
    }

    // A faulted VM stays stopped
    if (vm->last_error != VM_ERROR_NONE) {
        return vm->last_error;
    }
    if (vm->instruction_limit && vm->instruction_count >= vm->instruction_limit) {
        vm->error_pc = vm->registers[R3_PC];
        return vm_raise(vm, VM_ERROR_INSTRUCTION_LIMIT, "Instruction limit of %u reached", vm->instruction_limit);
    }

    if (cpu_deliver_interrupt(vm) && vm->last_error != VM_ERROR_NONE) {
        return vm->last_error;
    }

    uint32_t pc = vm->registers[R3_PC];
    vm->error_pc = pc;

    if (pc % 4 != 0) {
        return vm_raise(vm, VM_ERROR_INVALID_ALIGNMENT, "Unaligned program counter 0x%04X", pc);
    }
    if (memory_check_address_permissions(vm, pc, 4, PROT_EXEC) != VM_ERROR_NONE) {
        return vm->last_error;
    }
    uint32_t word = read_le32(vm->memory + pc);
    uint32_t size = isa_instruction_size(word);
    if (size > 4 && memory_check_address_permissions(vm, pc + 4, 4, PROT_EXEC) != VM_ERROR_NONE) {
        return vm->last_error;
    }

    Instruction instr;
    isa_decode(word, size > 4 ? read_le32(vm->memory + pc + 4) : 0, &instr);

    // PC already points at the next instruction while this one executes
    vm->registers[R3_PC] = pc + size;
    cpu_execute_instruction(vm, &instr);
    if (vm->last_error != VM_ERROR_NONE) {
        return vm->last_error;
    }

    vm->instruction_count++;
    io_tick(vm);
    return vm->last_error;
}

// Decodes the instruction at address without faulting the VM; returns its size, or 0 if it is out of range
uint32_t vm_peek_instruction(const VM *vm, uint32_t address, Instruction *instr) {
    if (address > vm->memory_size || vm->memory_size - address < 4) {
        return 0;
    }
    uint32_t word = read_le32(vm->memory + address);
    uint32_t size = isa_instruction_size(word);
    if (vm->memory_size - address < size) {
        return 0;
    }
    isa_decode(word, size > 4 ? read_le32(vm->memory + address + 4) : 0, instr);
    return size;
}

static int load_vm32_image(VM *vm, const uint8_t *image, uint32_t size) {
    Vm32Image bin;
    const char *problem = vm32_parse(image, size, &bin);
    if (problem) {
        return vm_raise(vm, VM_ERROR_IO_ERROR, "%s", problem);
    }

    // The program has to leave room for the stack at the top of memory
    uint64_t code_end = (uint64_t)bin.code_base + bin.code_size;
    uint64_t data_end = (uint64_t)bin.data_base + bin.data_size;
    uint32_t limit = vm->memory_size - VM_STACK_SIZE;
    if (code_end > limit || data_end > limit) {
        return vm_raise(vm, VM_ERROR_SEGMENTATION_FAULT, "Program does not fit below the stack in %u KB of memory",
                        vm->memory_size / 1024);
    }
    if (bin.code_size && bin.data_size && bin.data_base < code_end && bin.code_base < data_end) {
        return vm_raise(vm, VM_ERROR_SEGMENTATION_FAULT, "Code and data of the program overlap");
    }
    if (bin.entry < bin.code_base || bin.entry - bin.code_base >= bin.code_size) {
        return vm_raise(vm, VM_ERROR_SEGMENTATION_FAULT, "Entry point 0x%04X is outside the code", bin.entry);
    }

    memcpy(vm->memory + bin.code_base, bin.code, bin.code_size);
    memcpy(vm->memory + bin.data_base, bin.data, bin.data_size);

    if (bin.symbol_size > 0) {
        debug_info_free(vm->debug_info);
        vm->debug_info = debug_info_parse(bin.symbols, bin.symbol_size);
    }

    vm->entry_point = bin.entry;
    vm->code_end = (uint32_t)code_end;
    vm->image_end = (uint32_t)(bin.data_size && data_end > code_end ? data_end : code_end);
    cpu_reset(vm);
    return VM_ERROR_NONE;
}

// Raw images without a header are loaded as code at address 0
static int load_raw_image(VM *vm, const uint8_t *image, uint32_t size) {
    if (size > vm->memory_size - VM_STACK_SIZE) {
        return vm_raise(vm, VM_ERROR_SEGMENTATION_FAULT, "Raw program image does not fit below the stack");
    }

    memcpy(vm->memory, image, size);
    vm->entry_point = 0;
    vm->code_end = size;
    vm->image_end = size;
    cpu_reset(vm);
    return VM_ERROR_NONE;
}

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

const char *vm_get_error_string(int error_code) {
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
        case VM_ERROR_NESTED_INTERRUPT:
            return "Nested interrupt";
        case VM_ERROR_INSTRUCTION_LIMIT:
            return "Instruction limit reached";
        default:
            return "Unknown error";
    }
}

const char *vm_get_error_message(const VM *vm) {
    if (!vm) {
        return "Invalid VM pointer";
    }
    return vm->last_error == VM_ERROR_NONE ? "No error" : vm->error_message;
}
