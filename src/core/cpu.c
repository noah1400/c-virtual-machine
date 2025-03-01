#include <stdio.h>
#include <string.h>
#include "cpu.h"
#include "memory.h"
#include "instruction_set.h"
#include "decoder.h"

// CPU initialization
int cpu_init(VM *vm) {
    if (!vm) {
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    // Clear all registers
    memset(vm->registers, 0, sizeof(vm->registers));
    
    // Set up stack pointer to top of stack segment
    vm->registers[R2_SP] = STACK_SEGMENT_BASE + STACK_SEGMENT_SIZE;
    
    // Set up base pointer to the same value initially
    vm->registers[R1_BP] = vm->registers[R2_SP];
    
    // Set program counter to beginning of code segment
    vm->registers[R3_PC] = CODE_SEGMENT_BASE;
    
    // Clear status register
    vm->registers[R4_SR] = 0;
    
    // Clear VM state
    vm->halted = 0;
    vm->debug_mode = 0;
    vm->instruction_count = 0;
    vm->last_error = VM_ERROR_NONE;
    
    return VM_ERROR_NONE;
}

// Reset CPU to initial state
int cpu_reset(VM *vm) {
    return cpu_init(vm);
}

// Get register value
uint32_t cpu_get_register(VM *vm, uint8_t reg) {
    if (!vm || reg >= 16) {
        return 0;
    }
    return vm->registers[reg];
}

// Set register value
void cpu_set_register(VM *vm, uint8_t reg, uint32_t value) {
    if (!vm || reg >= 16) {
        return;
    }
    vm->registers[reg] = value;
}

// Get a flag from status register
uint8_t cpu_get_flag(VM *vm, uint8_t flag) {
    if (!vm) {
        return 0;
    }
    return (vm->registers[R4_SR] & flag) ? 1 : 0;
}

// Set a flag in status register
void cpu_set_flag(VM *vm, uint8_t flag, uint8_t value) {
    if (!vm) {
        return;
    }
    
    if (value) {
        vm->registers[R4_SR] |= flag;
    } else {
        vm->registers[R4_SR] &= ~flag;
    }
}

// Update flags based on result
void cpu_update_flags(VM *vm, uint32_t result, uint8_t flags_to_update) {
    if (!vm) {
        return;
    }
    
    // Zero flag
    if (flags_to_update & ZERO_FLAG) {
        cpu_set_flag(vm, ZERO_FLAG, result == 0);
    }
    
    // Negative flag
    if (flags_to_update & NEG_FLAG) {
        cpu_set_flag(vm, NEG_FLAG, (result & 0x80000000) != 0);
    }
    
    // Other flags are handled by specific instructions
}

// Push value onto stack
void cpu_stack_push(VM *vm, uint32_t value) {
    if (!vm) {
        return;
    }
    
    // Decrease stack pointer
    vm->registers[R2_SP] -= 4;
    
    // Check for stack overflow
    if (vm->registers[R2_SP] < STACK_SEGMENT_BASE) {
        vm->registers[R2_SP] += 4; // Restore SP
        vm->last_error = VM_ERROR_STACK_OVERFLOW;
        snprintf(vm->error_message, sizeof(vm->error_message), 
                 "Stack overflow");
        return;
    }
    
    // Write value to stack
    memory_write_dword(vm, vm->registers[R2_SP], value);
}

// Pop value from stack
uint32_t cpu_stack_pop(VM *vm) {
    if (!vm) {
        return 0;
    }
    
    // Check for stack underflow
    if (vm->registers[R2_SP] >= STACK_SEGMENT_BASE + STACK_SEGMENT_SIZE) {
        vm->last_error = VM_ERROR_STACK_UNDERFLOW;
        snprintf(vm->error_message, sizeof(vm->error_message), 
                 "Stack underflow");
        return 0;
    }
    
    // Read value from stack
    uint32_t value = memory_read_dword(vm, vm->registers[R2_SP]);
    
    // Increase stack pointer
    vm->registers[R2_SP] += 4;
    
    return value;
}

// Create a new stack frame
void cpu_enter_frame(VM *vm, uint16_t locals_size) {
    if (!vm) {
        return;
    }
    
    // Save old base pointer
    cpu_stack_push(vm, vm->registers[R1_BP]);
    
    // Update base pointer to current stack position
    vm->registers[R1_BP] = vm->registers[R2_SP];
    
    // Allocate space for local variables
    vm->registers[R2_SP] -= locals_size;
    
    // Check for stack overflow
    if (vm->registers[R2_SP] < STACK_SEGMENT_BASE) {
        // Restore SP and BP
        vm->registers[R2_SP] = vm->registers[R1_BP];
        vm->registers[R1_BP] = memory_read_dword(vm, vm->registers[R2_SP]);
        vm->registers[R2_SP] += 4;
        
        vm->last_error = VM_ERROR_STACK_OVERFLOW;
        snprintf(vm->error_message, sizeof(vm->error_message), 
                 "Stack overflow during frame creation");
        return;
    }
}

// Destroy current stack frame
void cpu_leave_frame(VM *vm) {
    if (!vm) {
        return;
    }
    
    // Restore stack pointer from base pointer
    vm->registers[R2_SP] = vm->registers[R1_BP];
    
    // Restore previous base pointer
    vm->registers[R1_BP] = cpu_stack_pop(vm);
}

// Execute a single instruction
int cpu_step(VM *vm) {
    if (!vm) {
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    // Check if VM is halted
    if (vm->halted) {
        return VM_ERROR_NONE;
    }
    
    // Fetch and decode instruction
    Instruction instr;
    int result = vm_decode_instruction(vm, vm->registers[R3_PC], &instr);
    if (result != VM_ERROR_NONE) {
        return result;
    }
    
    // Save current instruction for debugging
    vm->current_instr = instr;
    
    // Increment PC to next instruction
    vm->registers[R3_PC] += 4;
    
    // Execute instruction
    result = cpu_execute_instruction(vm, &instr);
    
    // Increment instruction count
    vm->instruction_count++;
    
    return result;
}

// Dump register state for debugging
void cpu_dump_registers(VM *vm) {
    if (!vm) {
        return;
    }
    
    printf("Register Dump:\n");
    
    // Print general purpose registers
    printf("R0(ACC): 0x%08X  R1(BP):  0x%08X  R2(SP):  0x%08X  R3(PC):  0x%08X\n", 
           vm->registers[R0_ACC], vm->registers[R1_BP], 
           vm->registers[R2_SP], vm->registers[R3_PC]);
    
    printf("R4(SR):  0x%08X  R5:      0x%08X  R6:      0x%08X  R7:      0x%08X\n", 
           vm->registers[R4_SR], vm->registers[R5], 
           vm->registers[R6], vm->registers[R7]);
    
    printf("R8:      0x%08X  R9:      0x%08X  R10:     0x%08X  R11:     0x%08X\n", 
           vm->registers[R8], vm->registers[R9], 
           vm->registers[R10], vm->registers[R11]);
    
    printf("R12:     0x%08X  R13:     0x%08X  R14:     0x%08X  R15(LR): 0x%08X\n", 
           vm->registers[R12], vm->registers[R13], 
           vm->registers[R14], vm->registers[R15_LR]);
    
    // Print flags
    printf("Flags: [%c%c%c%c%c%c%c%c]\n",
           (vm->registers[R4_SR] & ZERO_FLAG) ? 'Z' : '-',
           (vm->registers[R4_SR] & NEG_FLAG) ? 'N' : '-',
           (vm->registers[R4_SR] & CARRY_FLAG) ? 'C' : '-',
           (vm->registers[R4_SR] & OVER_FLAG) ? 'O' : '-',
           (vm->registers[R4_SR] & INT_FLAG) ? 'I' : '-',
           (vm->registers[R4_SR] & DIR_FLAG) ? 'D' : '-',
           (vm->registers[R4_SR] & SYS_FLAG) ? 'S' : '-',
           (vm->registers[R4_SR] & TRAP_FLAG) ? 'T' : '-');
    
    // Check registers for literal ASCII characters and potential string pointers
    // Skip R1(BP), R2(SP), R3(PC), R4(SR) as these have special purposes
    for (int i = 0; i < 16; i++) {
        if (i == R1_BP || i == R2_SP || i == R3_PC || i == R4_SR) {
            continue;
        }
        
        uint32_t value = vm->registers[i];
        
        // First check if the value could be a printable ASCII character
        // Check both the lower byte (common) and the whole value (less common)
        uint8_t low_byte = value & 0xFF;
        
        char register_name[10];
        if (i == R0_ACC) {
            strcpy(register_name, "R0(ACC)");
        } else if (i == R15_LR) {
            strcpy(register_name, "R15(LR)");
        } else {
            sprintf(register_name, "R%-2d    ", i);
        }
        
        // Check if lower byte is a printable ASCII or common control character
        if ((low_byte >= 32 && low_byte <= 126) || 
            low_byte == '\n' || low_byte == '\r' || low_byte == '\t') {
            
            // Special handling for newline, carriage return, and tab
            const char* char_repr;
            if (low_byte == '\n') char_repr = "\\n";
            else if (low_byte == '\r') char_repr = "\\r";
            else if (low_byte == '\t') char_repr = "\\t";
            else {
                static char single_char[2] = {0, 0};
                single_char[0] = (char)low_byte;
                char_repr = single_char;
            }
            
            printf("%s contains ASCII: '%s' (%d)\n", register_name, char_repr, low_byte);
            
            // If the value is only a character (higher bytes are 0), continue to next register
            if (value <= 0xFF) {
                continue;
            }
        }
        
        // Then check if it's a pointer to a string
        uint16_t addr = (uint16_t)(value & 0xFFFF);
        
        // Check if this register might point to a string
        if (memory_might_be_string(vm, addr)) {
            char* str = memory_extract_string(vm, addr, 40);
            if (str) {
                // Print string content (truncate long strings)
                if (strlen(str) > 30) {
                    str[27] = '.';
                    str[28] = '.';
                    str[29] = '.';
                    str[30] = '\0';
                }
                
                printf("%s points to string: \"%s\"\n", register_name, str);
                free(str);
            }
        }
    }
    
    // Print instruction information
    printf("Instruction count: %u\n", vm->instruction_count);
    
    // Add mnemonic to the last instruction output
    const char* mnemonic = vm_opcode_to_mnemonic(vm->current_instr.opcode);
    printf("Last instruction: OP=0x%02X (%s) MODE=0x%01X R1=0x%01X R2=0x%01X IMM=0x%03X\n",
           vm->current_instr.opcode, mnemonic,
           vm->current_instr.mode,
           vm->current_instr.reg1, vm->current_instr.reg2,
           vm->current_instr.immediate);
}

int cpu_execute_instruction(VM *vm, Instruction *instr) {
    extern int cpu_execute_instruction_impl(VM *vm, Instruction *instr);
    return cpu_execute_instruction_impl(vm, instr);
}