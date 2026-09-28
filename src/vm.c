#include <signal.h>
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

int vm_init(VM *vm, uint32_t memory_size, uint32_t stack_size) {
    memset(vm, 0, sizeof(*vm));
    vm->stack_size = stack_size;

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

static volatile sig_atomic_t stop_signal;

// A second signal ends the process if the VM has not stopped yet
static void request_stop(int number) {
    if (stop_signal) {
        signal(number, SIG_DFL);
        raise(number);
    }
    stop_signal = number;
}

// Lets SIGINT, SIGTERM and SIGHUP stop the VM before its next instruction instead of killing the
// process, so the devices can restore the terminal; blocking host calls return early
void vm_catch_signals(void) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = request_stop;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGHUP, &action, NULL);
}

// cpu_run stops at least this often to let vm_step notice a stop signal
#define RUN_CHUNK (1u << 20)

// How many instructions cpu_run may run before the next step, which is none while something has to happen
// between instructions: a stop signal, device ticks, a trap, an interrupt to deliver or paging
static uint32_t run_limit(const VM *vm) {
    uint32_t status = vm->registers[R4_SR], limit = RUN_CHUNK;
    if (stop_signal || vm->last_error != VM_ERROR_NONE || vm->io_ticking || vm->control[CR_PTB] ||
        (status & TRAP_FLAG) || (vm->irq_pending && (status & INT_FLAG))) {
        return 0;
    }
    if (vm->instruction_limit) {
        uint32_t left = vm->instruction_count < vm->instruction_limit ? vm->instruction_limit - vm->instruction_count : 0;
        limit = left < limit ? left : limit;
    }
    return limit;
}

// Runs the instructions that need no checks between them with cpu_run and steps through the others
int vm_run(VM *vm) {
    while (!vm->halted) {
        uint32_t limit = run_limit(vm);
        if (limit) {
            cpu_run(vm, limit);
        }
        int result = vm_step(vm);
        if (result != VM_ERROR_NONE) {
            return result;
        }
    }
    return VM_ERROR_NONE;
}

// Fetches, decodes and executes the instruction at PC
static int execute_next(VM *vm) {
    uint32_t pc = vm->registers[R3_PC];

    if (pc % 4 != 0) {
        return vm_raise(vm, VM_ERROR_INVALID_ALIGNMENT, "Unaligned program counter 0x%04X", pc);
    }
    uint32_t word, extension = 0;
    if (memory_fetch(vm, pc, &word) != VM_ERROR_NONE) {
        return vm->last_error;
    }
    uint32_t size = isa_instruction_size(word);
    if (size > 4 && memory_fetch(vm, pc + 4, &extension) != VM_ERROR_NONE) {
        return vm->last_error;
    }

    Instruction instr;
    isa_decode(word, extension, &instr);

    // PC already points at the next instruction while this one executes
    vm->registers[R3_PC] = pc + size;
    cpu_execute_instruction(vm, &instr);
    return vm->last_error;
}

int vm_step(VM *vm) {
    uint32_t saved[16];

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
    if (stop_signal) {
        vm->error_pc = vm->registers[R3_PC];
        vm->exit_code = 128 + (uint32_t)stop_signal;
        return vm_raise(vm, VM_ERROR_SIGNAL, "Stopped by signal %d", (int)stop_signal);
    }

    // Faults leave the registers as they were before the instruction, for a handler to resume from
    vm->entered_interrupt = 0;
    vm->exception = 0;
    memcpy(saved, vm->registers, sizeof(saved));
    vm->error_pc = vm->registers[R3_PC];
    if (vm->irq_pending && cpu_deliver_interrupt(vm)) {
        if (vm->last_error != VM_ERROR_NONE) {
            memcpy(vm->registers, saved, sizeof(saved));
            return cpu_exception(vm);
        }
        memcpy(saved, vm->registers, sizeof(saved));
        vm->error_pc = vm->registers[R3_PC];
    }

    int trap = cpu_get_flag(vm, TRAP_FLAG) && !vm->entered_interrupt;
    uint32_t status = vm->registers[R4_SR];
    if (execute_next(vm) != VM_ERROR_NONE) {
        memcpy(vm->registers, saved, sizeof(saved));
        return cpu_exception(vm);
    }

    // User mode cannot change the mode, interrupt or trap flags, except by entering a handler
    if (!(status & SYS_FLAG) && !vm->entered_interrupt) {
        vm->registers[R4_SR] = (vm->registers[R4_SR] & ~SR_PROTECTED) | (status & SR_PROTECTED);
    }

    vm->instruction_count++;
    if (vm->io_ticking) {
        io_tick(vm);
    }
    if (trap && !vm->halted && !vm->entered_interrupt && vm->last_error == VM_ERROR_NONE) {
        cpu_interrupt(vm, VM_TRAP_VECTOR);
    }
    return vm->last_error;
}

// Decodes the instruction at address without faulting the VM; returns its size, or 0 if it is out of range
uint32_t vm_peek_instruction(const VM *vm, uint32_t address, Instruction *instr) {
    uint8_t bytes[8];
    if (memory_peek(vm, address, bytes, 4) < 4) {
        return 0;
    }
    uint32_t size = isa_instruction_size(read_le32(bytes));
    if (size > 4 && memory_peek(vm, address + 4, bytes + 4, 4) < 4) {
        return 0;
    }
    isa_decode(read_le32(bytes), read_le32(bytes + 4), instr);
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
    uint32_t limit = vm->memory_size - vm->stack_size;
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
    if (size > vm->memory_size - vm->stack_size) {
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
    if (vm32_is_object(program, size)) {
        return vm_raise(vm, VM_ERROR_IO_ERROR, "This is an object file, which vmld has to link first");
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
        case VM_ERROR_PRIVILEGE:
            return "Privilege violation";
        case VM_ERROR_PAGE_FAULT:
            return "Page fault";
        case VM_ERROR_INSTRUCTION_LIMIT:
            return "Instruction limit reached";
        case VM_ERROR_SIGNAL:
            return "Stopped by a signal";
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
