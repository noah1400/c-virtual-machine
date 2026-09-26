#include <stdio.h>
#include "vm_types.h"
#include "memory.h"
#include "cpu.h"

// Implementation of stack operations (abstraction layer over CPU stack functions)

void vm_stack_push(VM *vm, uint32_t value) {
    cpu_stack_push(vm, value);
}

uint32_t vm_stack_pop(VM *vm) {
    return cpu_stack_pop(vm);
}

// Create a new stack frame
void vm_create_stack_frame(VM *vm, uint16_t params_size, uint16_t locals_size) {
    // Save return address
    cpu_stack_push(vm, vm->registers[R3_PC]);
    
    // Create frame with local variables space
    cpu_enter_frame(vm, locals_size);
}

// Destroy current stack frame and return to caller
void vm_destroy_stack_frame(VM *vm) {
    // Destroy the frame, restoring BP
    cpu_leave_frame(vm);
    
    // Pop return address and update PC
    vm->registers[R3_PC] = cpu_stack_pop(vm);
}

// Push all registers onto stack (except SP)
void vm_push_all_registers(VM *vm) {
    // Save all registers in reverse order
    for (int i = 15; i >= 0; i--) {
        if (i != R2_SP) {  // Don't push SP
            cpu_stack_push(vm, vm->registers[i]);
        } else {
            // Push original SP value (before any pushes)
            cpu_stack_push(vm, vm->registers[R2_SP] + 4 * 15);
        }
    }
}

// Pop all registers from stack
void vm_pop_all_registers(VM *vm) {
    uint32_t orig_sp = vm->registers[R2_SP];
    
    // Restore all registers
    for (int i = 0; i < 16; i++) {
        if (i != R2_SP) {  // Don't pop into SP
            vm->registers[i] = cpu_stack_pop(vm);
        } else {
            // Skip SP
            vm->registers[R2_SP] += 4;
        }
    }
}

// Push status register onto stack
void vm_push_flags(VM *vm) {
    cpu_stack_push(vm, vm->registers[R4_SR]);
}

// Pop status register from stack
void vm_pop_flags(VM *vm) {
    vm->registers[R4_SR] = cpu_stack_pop(vm);
}

// Dump stack contents for debugging
void vm_dump_stack(VM *vm, int num_entries) {
    uint16_t sp = vm->registers[R2_SP];
    uint16_t bp = vm->registers[R1_BP];
    
    printf("=== Stack Dump ===\n");
    printf("SP=0x%04X, BP=0x%04X\n", sp, bp);
    
    // Limit number of entries to prevent excessive output
    if (num_entries <= 0 || num_entries > 64) {
        num_entries = 16;
    }
    
    // Start from stack pointer and go up
    for (int i = 0; i < num_entries; i++) {
        uint16_t addr = sp + i * 4;
        
        // Don't go beyond stack segment
        if (addr >= STACK_SEGMENT_BASE + STACK_SEGMENT_SIZE) {
            break;
        }
        
        // Read stack entry
        uint32_t value = memory_read_dword(vm, addr);
        
        // Mark base pointer and frame entries
        if (addr == bp) {
            printf("BP-> 0x%04X: 0x%08X\n", addr, value);
        } else if (addr == bp + 4) {
            printf("RA-> 0x%04X: 0x%08X (Return Address)\n", addr, value);
        } else if (addr < sp) {
            printf("     0x%04X: 0x%08X (Below SP)\n", addr, value);
        } else {
            printf("     0x%04X: 0x%08X\n", addr, value);
        }
    }
}