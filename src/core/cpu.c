#include <stdio.h>
#include <string.h>
#include "cpu.h"
#include "disassembler.h"
#include "memory.h"
#include "vm.h"

#define STACK_TOP (STACK_SEGMENT_BASE + STACK_SEGMENT_SIZE)

void cpu_reset(VM *vm) {
    memset(vm->registers, 0, sizeof(vm->registers));
    vm->registers[R2_SP] = STACK_TOP;
    vm->registers[R1_BP] = STACK_TOP;
    vm->registers[R3_PC] = vm->entry_point;
    vm->halted = 0;
}

uint8_t cpu_get_flag(const VM *vm, uint8_t flag) {
    return (vm->registers[R4_SR] & flag) != 0;
}

void cpu_set_flag(VM *vm, uint8_t flag, int value) {
    if (value) {
        vm->registers[R4_SR] |= flag;
    } else {
        vm->registers[R4_SR] &= ~(uint32_t)flag;
    }
}

static int stack_pointer_valid(VM *vm, uint32_t sp) {
    if (sp < STACK_SEGMENT_BASE || sp > STACK_TOP) {
        vm_raise(vm, VM_ERROR_SEGMENTATION_FAULT, "Stack pointer 0x%08X is outside the stack segment", sp);
        return 0;
    }
    return 1;
}

void cpu_stack_push(VM *vm, uint32_t value) {
    uint32_t sp = vm->registers[R2_SP];
    if (!stack_pointer_valid(vm, sp)) {
        return;
    }
    if (sp - STACK_SEGMENT_BASE < 4) {
        vm_raise(vm, VM_ERROR_STACK_OVERFLOW, "Stack overflow");
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
    if (STACK_TOP - sp < 4) {
        vm_raise(vm, VM_ERROR_STACK_UNDERFLOW, "Stack underflow");
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
    if (locals_size > sp - STACK_SEGMENT_BASE) {
        vm->registers[R2_SP] = sp + 4;
        vm_raise(vm, VM_ERROR_STACK_OVERFLOW, "Stack overflow during frame creation");
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

// Pops registers saved by cpu_push_all, discarding the saved SP
void cpu_pop_all(VM *vm, int restore_pc) {
    for (int i = 0; i < 16 && vm->last_error == VM_ERROR_NONE; i++) {
        uint32_t value = cpu_stack_pop(vm);
        if (vm->last_error == VM_ERROR_NONE && i != R2_SP && (restore_pc || i != R3_PC)) {
            vm->registers[i] = value;
        }
    }
}

void cpu_interrupt(VM *vm, uint8_t vector) {
    // The vector table holds one 32-bit handler address per vector
    uint32_t handler = memory_read_dword(vm, INTERRUPT_VECTOR_TABLE + vector * 4u);
    if (vm->last_error != VM_ERROR_NONE) {
        return;
    }
    if (handler == 0) {
        vm_raise(vm, VM_ERROR_UNHANDLED_INTERRUPT, "Unhandled interrupt: %d", vector);
        return;
    }

    // Save the execution context and mask interrupts while the handler runs
    cpu_push_all(vm);
    vm->registers[R4_SR] &= ~(uint32_t)INT_FLAG;
    vm->registers[R3_PC] = handler;
}

// Latches a device interrupt; requests made while one is pending are merged
void cpu_request_interrupt(VM *vm, uint8_t vector) {
    vm->irq_pending = 1;
    vm->irq_vector = vector;
}

// Enters the handler of a pending device interrupt if interrupts are enabled; returns 1 if it did
int cpu_deliver_interrupt(VM *vm) {
    if (!vm->irq_pending || !cpu_get_flag(vm, INT_FLAG)) {
        return 0;
    }
    vm->irq_pending = 0;
    cpu_interrupt(vm, vm->irq_vector);
    return 1;
}

void cpu_enable_interrupts(VM *vm) {
    vm->registers[R4_SR] |= INT_FLAG;
}

void cpu_disable_interrupts(VM *vm) {
    vm->registers[R4_SR] &= ~(uint32_t)INT_FLAG;
}

// Copies a NUL-terminated run of at least three printable characters in the data or heap segment
static int describe_string(const VM *vm, uint32_t address, char *out, size_t size) {
    int in_data = address >= DATA_SEGMENT_BASE && address < DATA_SEGMENT_BASE + DATA_SEGMENT_SIZE;
    int in_heap = address >= HEAP_SEGMENT_BASE && address < HEAP_SEGMENT_BASE + HEAP_SEGMENT_SIZE;
    if (!in_data && !in_heap) {
        return 0;
    }

    size_t length = 0;
    for (uint32_t a = address; a < vm->memory_size && length < 64; a++, length++) {
        uint8_t c = vm->memory[a];
        if (c == 0) {
            break;
        }
        if ((c < 32 || c > 126) && c != '\n' && c != '\t') {
            return 0;
        }
    }
    if (length < 3 || length == 64) {
        return 0;
    }

    size_t shown = length < size - 4 ? length : size - 4;
    for (size_t i = 0; i < shown; i++) {
        uint8_t c = vm->memory[address + i];
        out[i] = c == '\n' || c == '\t' ? ' ' : (char)c;
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

    disasm_format(&vm->current_instr, vm->debug_info, text, sizeof(text));
    printf("Instructions executed: %u, last: %s\n", vm->instruction_count, text);
}
