#include <stdio.h>
#include <string.h>
#include "binfmt.h"
#include "cpu.h"
#include "debug.h"
#include "disassembler.h"
#include "memory.h"
#include "vm.h"

void cpu_reset(VM *vm) {
    memset(vm->registers, 0, sizeof(vm->registers));
    memset(vm->control, 0, sizeof(vm->control));
    vm->control[CR_SHI] = vm->memory_size;
    vm->control[CR_SLO] = vm->memory_size - VM_STACK_SIZE;
    vm->control[CR_HEAPLO] = (vm->image_end + 15) & ~15u;
    vm->control[CR_HEAPHI] = vm->control[CR_SLO];
    memory_heap_reset(vm);
    vm->registers[R2_SP] = vm->control[CR_SHI];
    vm->registers[R1_BP] = vm->control[CR_SHI];
    vm->registers[R3_PC] = vm->entry_point;
    vm->registers[R4_SR] = SYS_FLAG;
    vm->halted = 0;
    vm->call_depth = 0;
}

// The stack lies between the SLO and SHI control registers
static void stack_fault(VM *vm, int code, uint32_t address, const char *message) {
    if (vm->last_error == VM_ERROR_NONE) {
        vm->control[CR_FADDR] = address;
    }
    vm_raise(vm, code, message, address);
}

static int stack_pointer_valid(VM *vm, uint32_t sp) {
    if (sp < vm->control[CR_SLO] || sp > vm->control[CR_SHI]) {
        stack_fault(vm, VM_ERROR_SEGMENTATION_FAULT, sp, "Stack pointer 0x%08X is outside the stack");
        return 0;
    }
    return 1;
}

void cpu_stack_push(VM *vm, uint32_t value) {
    uint32_t sp = vm->registers[R2_SP];
    if (!stack_pointer_valid(vm, sp)) {
        return;
    }
    if (sp - vm->control[CR_SLO] < 4) {
        stack_fault(vm, VM_ERROR_STACK_OVERFLOW, sp - 4, "Stack overflow at 0x%08X");
        return;
    }

    vm->registers[R2_SP] = sp - 4;
    memory_write_dword(vm, sp - 4, value);
}

uint32_t cpu_stack_pop(VM *vm) {
    uint32_t sp = vm->registers[R2_SP];
    if (!stack_pointer_valid(vm, sp)) {
        return 0;
    }
    if (vm->control[CR_SHI] - sp < 4) {
        stack_fault(vm, VM_ERROR_STACK_UNDERFLOW, sp, "Stack underflow at 0x%08X");
        return 0;
    }

    vm->registers[R2_SP] = sp + 4;
    return memory_read_dword(vm, sp);
}

// Saves BP, points BP at the saved value and reserves locals_size bytes below it
void cpu_enter_frame(VM *vm, uint32_t locals_size) {
    cpu_stack_push(vm, vm->registers[R1_BP]);
    if (vm->last_error != VM_ERROR_NONE) {
        return;
    }

    uint32_t sp = vm->registers[R2_SP];
    if (locals_size > sp - vm->control[CR_SLO]) {
        vm->registers[R2_SP] = sp + 4;
        stack_fault(vm, VM_ERROR_STACK_OVERFLOW, sp - locals_size, "Stack overflow creating a frame at 0x%08X");
        return;
    }

    vm->registers[R1_BP] = sp;
    vm->registers[R2_SP] = sp - locals_size;
}

void cpu_leave_frame(VM *vm) {
    vm->registers[R2_SP] = vm->registers[R1_BP];
    uint32_t saved_bp = cpu_stack_pop(vm);
    if (vm->last_error == VM_ERROR_NONE) {
        vm->registers[R1_BP] = saved_bp;
    }
}

// Pushes R15 down to R0 so that [SP + 4 * n] holds Rn, saving SP as it was before the first push
void cpu_push_all(VM *vm) {
    uint32_t original_sp = vm->registers[R2_SP];

    for (int i = 15; i >= 0 && vm->last_error == VM_ERROR_NONE; i--) {
        cpu_stack_push(vm, i == R2_SP ? original_sp : vm->registers[i]);
    }
}

// Pops registers saved by cpu_push_all, discarding the saved SP and PC
void cpu_pop_all(VM *vm) {
    for (int i = 0; i < 16 && vm->last_error == VM_ERROR_NONE; i++) {
        uint32_t value = cpu_stack_pop(vm);
        if (vm->last_error == VM_ERROR_NONE && i != R2_SP && i != R3_PC) {
            vm->registers[i] = value;
        }
    }
}

// Restores every register an interrupt saved, SP included, which may take the CPU back to user mode
void cpu_return_from_interrupt(VM *vm) {
    uint32_t frame[16];
    for (int i = 0; i < 16; i++) {
        frame[i] = cpu_stack_pop(vm);
        if (vm->last_error != VM_ERROR_NONE) {
            return;
        }
    }
    memcpy(vm->registers, frame, sizeof(frame));
    cpu_pop_frames(vm, 1);
}

// The shadow call stack follows calls and interrupts for backtraces. When it is full, the
// outermost frame makes room.
void cpu_push_frame(VM *vm, uint32_t site, uint32_t resume, int vector) {
    if (vm->call_depth == VM_CALL_FRAMES) {
        memmove(vm->call_frames, vm->call_frames + 1, (VM_CALL_FRAMES - 1) * sizeof(CallFrame));
        vm->call_depth--;
    }
    vm->call_frames[vm->call_depth++] = (CallFrame){ site, vm->registers[R2_SP], resume, vector };
}

// A return drops the calls whose return address now lies below SP; an interrupt return also
// drops the innermost interrupt, as it may switch to another stack
void cpu_pop_frames(VM *vm, int interrupt_return) {
    while (vm->call_depth > 0) {
        const CallFrame *top = &vm->call_frames[vm->call_depth - 1];
        if (top->vector >= 0) {
            vm->call_depth -= interrupt_return != 0;
            return;
        }
        if (!interrupt_return && top->slot >= vm->registers[R2_SP]) {
            return;
        }
        vm->call_depth--;
    }
}

// The vector table holds one 32-bit handler address per vector; 0 means there is no handler. It
// belongs to the kernel, so it is read with supervisor rights even when user code is interrupted.
static uint32_t vector_handler(VM *vm, uint8_t vector) {
    uint32_t table = vm->control[CR_IVTB], status = vm->registers[R4_SR];
    if (!table) {
        return 0;
    }
    vm->registers[R4_SR] |= SYS_FLAG;
    uint32_t handler = memory_read_dword(vm, table + vector * 4u);
    vm->registers[R4_SR] = status;
    return handler;
}

// Saves the execution context and runs the handler in supervisor mode with interrupts and
// single-stepping off; an interrupt from user mode switches to the stack in KSP when it is set
static void enter_handler(VM *vm, uint32_t handler, uint8_t vector) {
    uint32_t frame[16];

    memcpy(frame, vm->registers, sizeof(frame));
    vm->registers[R4_SR] = (frame[R4_SR] | SYS_FLAG) & ~(uint32_t)(INT_FLAG | TRAP_FLAG);
    if (!(frame[R4_SR] & SYS_FLAG) && vm->control[CR_KSP]) {
        vm->registers[R2_SP] = vm->control[CR_KSP];
    }
    for (int i = 15; i >= 0 && vm->last_error == VM_ERROR_NONE; i--) {
        cpu_stack_push(vm, frame[i]);
    }
    if (vm->last_error == VM_ERROR_NONE) {
        cpu_push_frame(vm, vm->error_pc, frame[R3_PC], vector);
    }
    vm->registers[R3_PC] = handler;
    vm->entered_interrupt = 1;
}

void cpu_interrupt(VM *vm, uint8_t vector) {
    uint32_t handler = vector_handler(vm, vector);
    if (vm->last_error != VM_ERROR_NONE) {
        return;
    }
    if (handler == 0) {
        vm_raise(vm, VM_ERROR_UNHANDLED_INTERRUPT, "Unhandled interrupt: %d", vector);
        return;
    }
    enter_handler(vm, handler, vector);
}

// Hands the fault the VM stopped with to the handler of the vector its code names. The caller has
// restored the registers to their state before the faulting instruction. Returns the error left.
int cpu_exception(VM *vm) {
    int code = vm->last_error;
    char message[sizeof(vm->error_message)];

    if (code <= VM_ERROR_NONE || code >= VM_EXCEPTION_VECTORS) {
        return code;
    }
    snprintf(message, sizeof(message), "%s", vm->error_message);
    vm_clear_error(vm);

    uint32_t handler = vector_handler(vm, (uint8_t)code);
    if (vm->last_error != VM_ERROR_NONE || handler == 0) {
        vm_clear_error(vm);
        return vm_raise(vm, code, "%s", message);
    }

    enter_handler(vm, handler, (uint8_t)code);
    if (vm->last_error != VM_ERROR_NONE) {
        char detail[sizeof(vm->error_message)];
        int second = vm->last_error;
        snprintf(detail, sizeof(detail), "%s", vm->error_message);
        vm_clear_error(vm);
        return vm_raise(vm, second, "Double fault: %s while handling: %s", detail, message);
    }
    vm->exception = (uint8_t)code;
    snprintf(vm->exception_message, sizeof(vm->exception_message), "%s", message);
    return VM_ERROR_NONE;
}

// Latches a device interrupt; a request for a vector that is already waiting is merged with it
void cpu_request_interrupt(VM *vm, uint8_t vector) {
    vm->irq_mask[vector / 32] |= 1u << (vector % 32);
    vm->irq_pending = 1;
}

// Enters the handler of the lowest waiting device interrupt if interrupts are enabled; returns 1 if it did
int cpu_deliver_interrupt(VM *vm) {
    if (!vm->irq_pending || !cpu_get_flag(vm, INT_FLAG)) {
        return 0;
    }

    int vector = -1;
    vm->irq_pending = 0;
    for (int word = 0; word < 8; word++) {
        uint32_t bits = vm->irq_mask[word];
        if (bits && vector < 0) {
            int bit = 0;
            while (!(bits & (1u << bit))) {
                bit++;
            }
            vm->irq_mask[word] = bits & ~(1u << bit);
            vector = word * 32 + bit;
        }
        vm->irq_pending |= vm->irq_mask[word] != 0;
    }
    if (vector < 0) {
        return 0;
    }
    cpu_interrupt(vm, (uint8_t)vector);
    return 1;
}

void cpu_enable_interrupts(VM *vm) {
    vm->registers[R4_SR] |= INT_FLAG;
}

void cpu_disable_interrupts(VM *vm) {
    vm->registers[R4_SR] &= ~(uint32_t)INT_FLAG;
}

void cpu_print_location(const VM *vm, FILE *out, uint32_t address) {
    char text[160];
    debug_describe(vm->debug_info, address, text, sizeof(text));
    fprintf(out, "0x%04X%s%s", address, text[0] ? " " : "", text);
    const SourceLine *line = debug_line_at(vm->debug_info, address);
    if (line && line->address == address) {
        fprintf(out, " %s:%u", line->source_file ? line->source_file : "?", line->line_num);
    }
    Instruction instr;
    if (address % 4 == 0 && vm_peek_instruction(vm, address, &instr)) {
        disasm_format(&instr, address, vm->debug_info, text, sizeof(text));
        fprintf(out, ": %s", text);
    }
}

static int frame_live(const VM *vm, const CallFrame *frame) {
    uint8_t saved[4];
    uint32_t check = frame->vector >= 0 ? frame->slot + 4 * R3_PC : frame->slot;
    return memory_peek(vm, check, saved, sizeof(saved)) == sizeof(saved) && read_le32(saved) == frame->resume;
}

// Lists the calls and interrupts that execution is nested in, innermost first, folding repeats of
// the same call. A frame whose return address is gone from the stack was left without returning.
void cpu_print_backtrace(const VM *vm, FILE *out, const char *prefix) {
    const CallFrame *shown = NULL;
    uint32_t number = 0, repeats = 0;

    for (uint32_t i = vm->call_depth; i-- > 0;) {
        const CallFrame *frame = &vm->call_frames[i];
        if (!frame_live(vm, frame)) {
            continue;
        }
        number++;
        if (shown && frame->site == shown->site && frame->vector == shown->vector) {
            repeats++;
            continue;
        }
        if (repeats) {
            fprintf(out, "%s... the same %u more times\n", prefix, repeats);
            repeats = 0;
        }
        fprintf(out, "%s#%u ", prefix, number);
        cpu_print_location(vm, out, frame->site);
        if (frame->vector >= 0) {
            fprintf(out, " (interrupt %d)", frame->vector);
        }
        fprintf(out, "\n");
        shown = frame;
    }
    if (repeats) {
        fprintf(out, "%s... the same %u more times\n", prefix, repeats);
    }
    if (vm->call_depth == VM_CALL_FRAMES) {
        fprintf(out, "%s... outer frames were not kept\n", prefix);
    }
}

// Copies a NUL-terminated run of at least three printable characters found after the code
static int describe_string(const VM *vm, uint32_t address, char *out, size_t size) {
    uint8_t bytes[64];
    uint32_t readable = address >= vm->code_end ? memory_peek(vm, address, bytes, sizeof(bytes)) : 0;

    size_t length = 0;
    while (length < readable && bytes[length] != 0) {
        uint8_t c = bytes[length++];
        if ((c < 32 || c > 126) && c != '\n' && c != '\t') {
            return 0;
        }
    }
    if (length < 3 || length == readable) {
        return 0;
    }

    size_t shown = length < size - 4 ? length : size - 4;
    for (size_t i = 0; i < shown; i++) {
        out[i] = bytes[i] == '\n' || bytes[i] == '\t' ? ' ' : (char)bytes[i];
    }
    strcpy(out + shown, shown < length ? "..." : "");
    return 1;
}

void cpu_dump_registers(VM *vm) {
    for (int i = 0; i < 16; i++) {
        printf("%-3s 0x%08X%s", isa_register_name((uint8_t)i), vm->registers[i], i % 4 == 3 ? "\n" : "   ");
    }

    static const struct {
        uint8_t flag;
        char name;
    } flags[] = {
        { ZERO_FLAG, 'Z' }, { NEG_FLAG, 'N' }, { CARRY_FLAG, 'C' }, { OVER_FLAG, 'O' },
        { INT_FLAG, 'I' }, { DIR_FLAG, 'D' }, { SYS_FLAG, 'S' }, { TRAP_FLAG, 'T' },
    };
    printf("Flags: [");
    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) {
        putchar(cpu_get_flag(vm, flags[i].flag) ? flags[i].name : '-');
    }
    printf("]\n");

    // Point out registers that hold a character or point at a string
    char text[160];
    for (int i = 0; i < 16; i++) {
        uint32_t value = vm->registers[i];
        if (i == R1_BP || i == R2_SP || i == R3_PC || i == R4_SR) {
            continue;
        }
        if (value >= 32 && value <= 126) {
            printf("%s = %u '%c'\n", isa_register_name((uint8_t)i), value, (char)value);
        } else if (describe_string(vm, value, text, 44)) {
            printf("%s -> \"%s\"\n", isa_register_name((uint8_t)i), text);
        }
    }

    disasm_format(&vm->current_instr, vm->error_pc, vm->debug_info, text, sizeof(text));
    printf("Instructions executed: %u, last: %s\n", vm->instruction_count, text);
}
