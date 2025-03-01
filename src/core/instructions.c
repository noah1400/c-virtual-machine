#include <stdio.h>
#include <stdlib.h>
#include "cpu.h"
#include "memory.h"
#include "instruction_set.h"
#include "vm_types.h"

// Forward declarations of instruction handlers
static int handle_nop(VM *vm, Instruction *instr);
static int handle_load(VM *vm, Instruction *instr);
static int handle_store(VM *vm, Instruction *instr);
static int handle_move(VM *vm, Instruction *instr);
static int handle_arithmetic(VM *vm, Instruction *instr);
static int handle_logical(VM *vm, Instruction *instr);
static int handle_jump(VM *vm, Instruction *instr);
static int handle_stack(VM *vm, Instruction *instr);
static int handle_system(VM *vm, Instruction *instr);
static int handle_memory(VM *vm, Instruction *instr);

// Helper function to get operand value based on addressing mode
static uint32_t get_operand_value(VM *vm, Instruction *instr, int is_second_operand) {
    uint8_t mode = instr->mode;
    uint8_t reg = is_second_operand ? instr->reg2 : instr->reg1;
    uint16_t imm = instr->immediate;
    uint32_t addr;
    
    switch (mode) {
        case IMM_MODE:
            return imm;
            
        case REG_MODE:
            return vm->registers[reg];
            
        case MEM_MODE:
            return memory_read_dword(vm, imm);
            
        case REGM_MODE:
            addr = vm->registers[reg];
            return memory_read_dword(vm, addr);
            
        case IDX_MODE:
            addr = vm->registers[reg] + imm;
            return memory_read_dword(vm, addr);
            
        case STK_MODE:
            addr = vm->registers[R2_SP] + imm;
            return memory_read_dword(vm, addr);
            
        case BAS_MODE:
            addr = vm->registers[R1_BP] + imm;
            return memory_read_dword(vm, addr);
            
        default:
            // Invalid addressing mode
            vm->last_error = VM_ERROR_INVALID_INSTRUCTION;
            snprintf(vm->error_message, sizeof(vm->error_message), 
                     "Invalid addressing mode: 0x%01X", mode);
            return 0;
    }
}

// Helper function to get target address for store operations
static uint16_t get_store_address(VM *vm, Instruction *instr, int is_second_operand) {
    uint8_t mode = instr->mode;
    uint8_t reg = is_second_operand ? instr->reg2 : instr->reg1;
    uint16_t imm = instr->immediate;
    
    switch (mode) {
        case MEM_MODE:
            return imm;
            
        case REGM_MODE:
            return vm->registers[reg];
            
        case IDX_MODE:
            return vm->registers[reg] + imm;
            
        case STK_MODE:
            return vm->registers[R2_SP] + imm;
            
        case BAS_MODE:
            return vm->registers[R1_BP] + imm;
            
        default:
            // Invalid addressing mode for store
            vm->last_error = VM_ERROR_INVALID_INSTRUCTION;
            snprintf(vm->error_message, sizeof(vm->error_message), 
                     "Invalid addressing mode for store: 0x%01X", mode);
            return 0;
    }
}

// Main instruction execution function
int cpu_execute_instruction_impl(VM *vm, Instruction *instr) {
    if (!vm || !instr) {
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    // Store current instruction for debugging
    vm->current_instr = *instr;
    
    // Dispatch based on instruction opcode
    uint8_t opcode = instr->opcode;
    
    // Data Transfer Instructions (0x00-0x1F)
    if (opcode <= 0x1F) {
        switch (opcode) {
            case NOP_OP:
                return handle_nop(vm, instr);
            case LOAD_OP:
            case LOADB_OP:
            case LOADW_OP:
            case LEA_OP:
                return handle_load(vm, instr);
            case STORE_OP:
            case STOREB_OP:
            case STOREW_OP:
                return handle_store(vm, instr);
            case MOVE_OP:
                return handle_move(vm, instr);
            default:
                // Unimplemented data transfer instruction
                vm->last_error = VM_ERROR_INVALID_INSTRUCTION;
                snprintf(vm->error_message, sizeof(vm->error_message), 
                         "Unimplemented data transfer instruction: 0x%02X", opcode);
                return VM_ERROR_INVALID_INSTRUCTION;
        }
    }
    
    // Arithmetic Instructions (0x20-0x3F)
    else if (opcode <= 0x3F) {
        return handle_arithmetic(vm, instr);
    }
    
    // Logical Instructions (0x40-0x5F)
    else if (opcode <= 0x5F) {
        return handle_logical(vm, instr);
    }
    
    // Control Flow Instructions (0x60-0x7F)
    else if (opcode <= 0x7F) {
        return handle_jump(vm, instr);
    }
    
    // Stack Instructions (0x80-0x9F)
    else if (opcode <= 0x9F) {
        return handle_stack(vm, instr);
    }
    
    // System Instructions (0xA0-0xBF)
    else if (opcode <= 0xBF) {
        return handle_system(vm, instr);
    }
    
    // Memory Control Instructions (0xC0-0xDF)
    else if (opcode <= 0xDF) {
        return handle_memory(vm, instr);
    }
    
    // Invalid opcode
    vm->last_error = VM_ERROR_INVALID_INSTRUCTION;
    snprintf(vm->error_message, sizeof(vm->error_message), 
             "Invalid opcode: 0x%02X", opcode);
    return VM_ERROR_INVALID_INSTRUCTION;
}

// Handle NOP instruction
static int handle_nop(VM *vm, Instruction *instr) {
    // Do nothing
    return VM_ERROR_NONE;
}

// Handle LOAD instructions
static int handle_load(VM *vm, Instruction *instr) {
    uint8_t opcode = instr->opcode;
    uint8_t dest_reg = instr->reg1;
    uint32_t value;
    
    switch (opcode) {
        case LOAD_OP:
            // Load 32-bit value into register
            value = get_operand_value(vm, instr, 0);
            vm->registers[dest_reg] = value;
            break;
            
        case LOADB_OP:
            // Load 8-bit value into register (zero-extended)
            if (instr->mode == IMM_MODE) {
                value = instr->immediate & 0xFF;
            } else {
                uint16_t addr = get_store_address(vm, instr, 0);
                value = memory_read_byte(vm, addr);
            }
            vm->registers[dest_reg] = value;
            break;
            
        case LOADW_OP:
            // Load 16-bit value into register (zero-extended)
            if (instr->mode == IMM_MODE) {
                value = instr->immediate & 0xFFFF;
            } else {
                uint16_t addr = get_store_address(vm, instr, 0);
                value = memory_read_word(vm, addr);
            }
            vm->registers[dest_reg] = value;
            break;
            
        case LEA_OP:
            // Load effective address into register
            vm->registers[dest_reg] = get_store_address(vm, instr, 0);
            break;
    }
    
    return VM_ERROR_NONE;
}

// Handle STORE instructions
static int handle_store(VM *vm, Instruction *instr) {
    uint8_t opcode = instr->opcode;
    uint8_t src_reg = instr->reg1;
    uint32_t value = vm->registers[src_reg];
    uint16_t addr = get_store_address(vm, instr, 1);
    
    switch (opcode) {
        case STORE_OP:
            // Store 32-bit value from register to memory
            memory_write_dword(vm, addr, value);
            break;
            
        case STOREB_OP:
            // Store low 8 bits from register to memory
            memory_write_byte(vm, addr, (uint8_t)(value & 0xFF));
            break;
            
        case STOREW_OP:
            // Store low 16 bits from register to memory
            memory_write_word(vm, addr, (uint16_t)(value & 0xFFFF));
            break;
    }
    
    return VM_ERROR_NONE;
}

// Handle MOVE instruction
static int handle_move(VM *vm, Instruction *instr) {
    // Simple register-to-register copy
    uint8_t src_reg = instr->reg2;
    uint8_t dest_reg = instr->reg1;
    
    vm->registers[dest_reg] = vm->registers[src_reg];
    
    return VM_ERROR_NONE;
}

// Handle arithmetic instructions
static int handle_arithmetic(VM *vm, Instruction *instr) {
    uint8_t opcode = instr->opcode;
    uint8_t dest_reg = instr->reg1;
    uint32_t operand1 = vm->registers[dest_reg];
    uint32_t operand2 = 0;
    uint32_t result = 0;
    uint8_t carry = 0;
    
    // Get the second operand based on addressing mode
    if (opcode != INC_OP && opcode != DEC_OP && opcode != NEG_OP) {
        operand2 = get_operand_value(vm, instr, 1);
    }
    
    switch (opcode) {
        case ADD_OP:
            // Add without carry
            result = operand1 + operand2;
            
            // Set carry flag if overflow in unsigned arithmetic
            carry = (result < operand1);
            cpu_set_flag(vm, CARRY_FLAG, carry);
            
            // Set overflow flag if overflow in signed arithmetic
            cpu_set_flag(vm, OVER_FLAG, 
                        ((operand1 & 0x80000000) == (operand2 & 0x80000000)) && 
                        ((result & 0x80000000) != (operand1 & 0x80000000)));
            
            vm->registers[dest_reg] = result;
            break;
            
        case SUB_OP:
            // Subtract without borrow
            result = operand1 - operand2;
            
            // Set carry flag if borrow occurred
            carry = (operand1 < operand2);
            cpu_set_flag(vm, CARRY_FLAG, carry);
            
            // Set overflow flag if overflow in signed arithmetic
            cpu_set_flag(vm, OVER_FLAG, 
                        ((operand1 & 0x80000000) != (operand2 & 0x80000000)) && 
                        ((result & 0x80000000) != (operand1 & 0x80000000)));
            
            vm->registers[dest_reg] = result;
            break;
            
        case MUL_OP:
            // Unsigned multiply
            result = operand1 * operand2;
            
            // Set overflow flag if high bits are lost
            cpu_set_flag(vm, OVER_FLAG, ((uint64_t)operand1 * (uint64_t)operand2) > 0xFFFFFFFF);
            
            vm->registers[dest_reg] = result;
            break;
            
        case DIV_OP:
            // Unsigned divide
            if (operand2 == 0) {
                vm->last_error = VM_ERROR_DIVISION_BY_ZERO;
                snprintf(vm->error_message, sizeof(vm->error_message), 
                         "Division by zero");
                return VM_ERROR_DIVISION_BY_ZERO;
            }
            
            result = operand1 / operand2;
            vm->registers[dest_reg] = result;
            break;
            
        case MOD_OP:
            // Modulo operation
            if (operand2 == 0) {
                vm->last_error = VM_ERROR_DIVISION_BY_ZERO;
                snprintf(vm->error_message, sizeof(vm->error_message), 
                         "Modulo by zero");
                return VM_ERROR_DIVISION_BY_ZERO;
            }
            
            result = operand1 % operand2;
            vm->registers[dest_reg] = result;
            break;
            
        case INC_OP:
            // Increment
            result = operand1 + 1;
            
            // Set overflow flag for signed overflow
            cpu_set_flag(vm, OVER_FLAG, (operand1 == 0x7FFFFFFF));
            
            vm->registers[dest_reg] = result;
            break;
            
        case DEC_OP:
            // Decrement
            result = operand1 - 1;
            
            // Set overflow flag for signed overflow
            cpu_set_flag(vm, OVER_FLAG, (operand1 == 0x80000000));
            
            vm->registers[dest_reg] = result;
            break;
            
        case NEG_OP:
            // Negate (two's complement)
            result = ~operand1 + 1;
            
            // Set overflow flag for MIN_INT
            cpu_set_flag(vm, OVER_FLAG, (operand1 == 0x80000000));
            
            vm->registers[dest_reg] = result;
            break;
            
        case CMP_OP:
            // Compare (subtract without storing result)
            result = operand1 - operand2;
            
            // Set carry flag if borrow occurred
            carry = (operand1 < operand2);
            cpu_set_flag(vm, CARRY_FLAG, carry);
            
            // Set overflow flag if overflow in signed arithmetic
            cpu_set_flag(vm, OVER_FLAG, 
                        ((operand1 & 0x80000000) != (operand2 & 0x80000000)) && 
                        ((result & 0x80000000) != (operand1 & 0x80000000)));
            
            // Don't store the result, just update flags
            break;
            
        case ADDC_OP:
            // Add with carry
            carry = cpu_get_flag(vm, CARRY_FLAG);
            result = operand1 + operand2 + carry;
            
            // Set carry flag
            cpu_set_flag(vm, CARRY_FLAG, (result < operand1) || (carry && result == operand1));
            
            // Set overflow flag
            cpu_set_flag(vm, OVER_FLAG, 
                        ((operand1 & 0x80000000) == (operand2 & 0x80000000)) && 
                        ((result & 0x80000000) != (operand1 & 0x80000000)));
            
            vm->registers[dest_reg] = result;
            break;
            
        case SUBC_OP:
            // Subtract with borrow
            carry = cpu_get_flag(vm, CARRY_FLAG);
            result = operand1 - operand2 - carry;
            
            // Set carry flag
            cpu_set_flag(vm, CARRY_FLAG, 
                        (operand1 < operand2) || (carry && operand1 == operand2));
            
            // Set overflow flag
            cpu_set_flag(vm, OVER_FLAG, 
                        ((operand1 & 0x80000000) != (operand2 & 0x80000000)) && 
                        ((result & 0x80000000) != (operand1 & 0x80000000)));
            
            vm->registers[dest_reg] = result;
            break;
            
        default:
            // Unimplemented arithmetic instruction
            vm->last_error = VM_ERROR_INVALID_INSTRUCTION;
            snprintf(vm->error_message, sizeof(vm->error_message), 
                     "Unimplemented arithmetic instruction: 0x%02X", opcode);
            return VM_ERROR_INVALID_INSTRUCTION;
    }
    
    // Update zero and negative flags for most operations
    if (opcode != CMP_OP) {
        cpu_update_flags(vm, result, ZERO_FLAG | NEG_FLAG);
    } else {
        // For CMP, we still update ZF and NF based on the temporary result
        cpu_update_flags(vm, result, ZERO_FLAG | NEG_FLAG);
    }
    
    return VM_ERROR_NONE;
}

// Handle logical instructions
static int handle_logical(VM *vm, Instruction *instr) {
    uint8_t opcode = instr->opcode;
    uint8_t dest_reg = instr->reg1;
    uint32_t operand1 = vm->registers[dest_reg];
    uint32_t operand2 = 0;
    uint32_t result = 0;
    uint8_t count = 0;
    
    // Get the second operand based on addressing mode
    if (opcode != NOT_OP) {
        operand2 = get_operand_value(vm, instr, 1);
    }
    
    switch (opcode) {
        case AND_OP:
            // Bitwise AND
            result = operand1 & operand2;
            vm->registers[dest_reg] = result;
            break;
            
        case OR_OP:
            // Bitwise OR
            result = operand1 | operand2;
            vm->registers[dest_reg] = result;
            break;
            
        case XOR_OP:
            // Bitwise XOR
            result = operand1 ^ operand2;
            vm->registers[dest_reg] = result;
            break;
            
        case NOT_OP:
            // Bitwise NOT
            result = ~operand1;
            vm->registers[dest_reg] = result;
            break;
            
        case SHL_OP:
            // Shift left
            count = operand2 & 0x1F;  // Only use low 5 bits for shift count
            
            // Set carry flag to the last bit shifted out
            if (count > 0) {
                cpu_set_flag(vm, CARRY_FLAG, (operand1 >> (32 - count)) & 1);
            }
            
            result = operand1 << count;
            vm->registers[dest_reg] = result;
            break;
            
        case SHR_OP:
            // Logical shift right
            count = operand2 & 0x1F;  // Only use low 5 bits for shift count
            
            // Set carry flag to the last bit shifted out
            if (count > 0) {
                cpu_set_flag(vm, CARRY_FLAG, (operand1 >> (count - 1)) & 1);
            }
            
            result = operand1 >> count;
            vm->registers[dest_reg] = result;
            break;
            
        case SAR_OP:
            // Arithmetic shift right (preserves sign bit)
            count = operand2 & 0x1F;  // Only use low 5 bits for shift count
            
            // Set carry flag to the last bit shifted out
            if (count > 0) {
                cpu_set_flag(vm, CARRY_FLAG, (operand1 >> (count - 1)) & 1);
            }
            
            // Arithmetic shift with sign extension
            if ((operand1 & 0x80000000) && count > 0) {
                // Sign bit is 1, need to extend it
                result = (operand1 >> count) | (0xFFFFFFFF << (32 - count));
            } else {
                // Sign bit is 0, behaves like logical shift
                result = operand1 >> count;
            }
            
            vm->registers[dest_reg] = result;
            break;
            
        case ROL_OP:
            // Rotate left
            count = operand2 & 0x1F;  // Only use low 5 bits for rotation count
            
            if (count > 0) {
                result = (operand1 << count) | (operand1 >> (32 - count));
                // Set carry flag to the last bit rotated
                cpu_set_flag(vm, CARRY_FLAG, result & 1);
            } else {
                result = operand1;
            }
            
            vm->registers[dest_reg] = result;
            break;
            
        case ROR_OP:
            // Rotate right
            count = operand2 & 0x1F;  // Only use low 5 bits for rotation count
            
            if (count > 0) {
                result = (operand1 >> count) | (operand1 << (32 - count));
                // Set carry flag to the last bit rotated
                cpu_set_flag(vm, CARRY_FLAG, (result >> 31) & 1);
            } else {
                result = operand1;
            }
            
            vm->registers[dest_reg] = result;
            break;
            
        case TEST_OP:
            // Test bits (AND without storing result)
            result = operand1 & operand2;
            // Don't store the result, just update flags
            break;
            
        default:
            // Unimplemented logical instruction
            vm->last_error = VM_ERROR_INVALID_INSTRUCTION;
            snprintf(vm->error_message, sizeof(vm->error_message), 
                     "Unimplemented logical instruction: 0x%02X", opcode);
            return VM_ERROR_INVALID_INSTRUCTION;
    }
    
    // Update zero and negative flags
    cpu_update_flags(vm, result, ZERO_FLAG | NEG_FLAG);
    
    return VM_ERROR_NONE;
}

// Handle jump and control flow instructions
static int handle_jump(VM *vm, Instruction *instr) {
    uint8_t opcode = instr->opcode;
    uint32_t target = 0;
    uint8_t condition = 0;
    uint8_t reg;
    
    // Get jump target address based on addressing mode
    if (instr->mode == IMM_MODE) {
        target = instr->immediate;
    } else if (instr->mode == REG_MODE) {
        target = vm->registers[instr->reg1];
    } else {
        target = get_operand_value(vm, instr, 0);
    }
    
    switch (opcode) {
        case JMP_OP:
            // Unconditional jump
            vm->registers[R3_PC] = target;
            break;
            
        case JZ_OP:
            // Jump if zero
            condition = cpu_get_flag(vm, ZERO_FLAG);
            if (condition) {
                vm->registers[R3_PC] = target;
            }
            break;
            
        case JNZ_OP:
            // Jump if not zero
            condition = !cpu_get_flag(vm, ZERO_FLAG);
            if (condition) {
                vm->registers[R3_PC] = target;
            }
            break;
            
        case JN_OP:
            // Jump if negative
            condition = cpu_get_flag(vm, NEG_FLAG);
            if (condition) {
                vm->registers[R3_PC] = target;
            }
            break;
            
        case JP_OP:
            // Jump if positive
            condition = !cpu_get_flag(vm, NEG_FLAG) && !cpu_get_flag(vm, ZERO_FLAG);
            if (condition) {
                vm->registers[R3_PC] = target;
            }
            break;
            
        case JO_OP:
            // Jump if overflow
            condition = cpu_get_flag(vm, OVER_FLAG);
            if (condition) {
                vm->registers[R3_PC] = target;
            }
            break;
            
        case JC_OP:
            // Jump if carry
            condition = cpu_get_flag(vm, CARRY_FLAG);
            if (condition) {
                vm->registers[R3_PC] = target;
            }
            break;
            
        case JBE_OP:
            // Jump if below or equal (unsigned)
            condition = cpu_get_flag(vm, CARRY_FLAG) || cpu_get_flag(vm, ZERO_FLAG);
            if (condition) {
                vm->registers[R3_PC] = target;
            }
            break;
            
        case JA_OP:
            // Jump if above (unsigned)
            condition = !cpu_get_flag(vm, CARRY_FLAG) && !cpu_get_flag(vm, ZERO_FLAG);
            if (condition) {
                vm->registers[R3_PC] = target;
            }
            break;
            
        case CALL_OP:
            // Call subroutine
            // Push return address (current PC) onto stack
            cpu_stack_push(vm, vm->registers[R3_PC]);
            // Jump to target
            vm->registers[R3_PC] = target;
            break;
            
        case RET_OP:
            // Return from subroutine
            // Pop return address from stack
            vm->registers[R3_PC] = cpu_stack_pop(vm);
            
            // Optional: Adjust stack for parameters
            if (instr->immediate > 0) {
                vm->registers[R2_SP] += instr->immediate;
            }
            break;
            
        case SYSCALL_OP:
            // System call
            // TODO
            {
                uint16_t syscall_num = instr->immediate;
                
                switch (syscall_num) {
                    case 0:  // Print character
                        printf("%c", (char)vm->registers[R0_ACC]);
                        break;
                        
                    case 1:  // Print integer
                        printf("%d", vm->registers[R0_ACC]);
                        break;
                        
                    case 2:  // Print string
                        {
                            uint16_t addr = vm->registers[R0_ACC];
                            char c;
                            
                            while ((c = memory_read_byte(vm, addr)) != 0) {
                                printf("%c", c);
                                addr++;
                            }
                        }
                        break;
                        
                    case 3:  // Read character
                        {
                            int c = getchar();
                            vm->registers[R0_ACC] = (c == EOF) ? 0 : c;
                        }
                        break;
                        
                    default:
                        vm->last_error = VM_ERROR_INVALID_SYSCALL;
                        snprintf(vm->error_message, sizeof(vm->error_message), 
                                 "Invalid system call: %d", syscall_num);
                        return VM_ERROR_INVALID_SYSCALL;
                }
            }
            break;
            
        case LOOP_OP:
            // Loop instruction (decrement and jump if not zero)
            reg = instr->reg1;
            vm->registers[reg]--;
            
            if (vm->registers[reg] != 0) {
                vm->registers[R3_PC] = target;
            }
            break;
            
        default:
            // Unimplemented control flow instruction
            vm->last_error = VM_ERROR_INVALID_INSTRUCTION;
            snprintf(vm->error_message, sizeof(vm->error_message), 
                     "Unimplemented control flow instruction: 0x%02X", opcode);
            return VM_ERROR_INVALID_INSTRUCTION;
    }
    
    return VM_ERROR_NONE;
}

// Handle stack operations
static int handle_stack(VM *vm, Instruction *instr) {
    uint8_t opcode = instr->opcode;
    uint32_t value;
    
    switch (opcode) {
        case PUSH_OP:
            // Push value onto stack
            if (instr->mode == IMM_MODE) {
                // Push immediate value
                value = instr->immediate;
            } else {
                // Push register value
                value = vm->registers[instr->reg1];
            }
            
            cpu_stack_push(vm, value);
            break;
            
        case POP_OP:
            // Pop value from stack into register
            value = cpu_stack_pop(vm);
            vm->registers[instr->reg1] = value;
            break;
            
        case PUSHF_OP:
            // Push flags onto stack
            cpu_stack_push(vm, vm->registers[R4_SR]);
            break;
            
        case POPF_OP:
            // Pop flags from stack
            vm->registers[R4_SR] = cpu_stack_pop(vm);
            break;
            
        case PUSHA_OP:
            // Push all registers onto stack
            for (int i = 0; i < 16; i++) {
                if (i != R2_SP) {  // Don't push SP
                    cpu_stack_push(vm, vm->registers[i]);
                } else {
                    // Push original SP value
                    cpu_stack_push(vm, vm->registers[R2_SP] + 4 * 15);
                }
            }
            break;
            
        case POPA_OP:
            // Pop all registers from stack (in reverse order)
            {
                uint32_t orig_sp = vm->registers[R2_SP];
                
                for (int i = 15; i >= 0; i--) {
                    if (i != R2_SP) {  // Don't pop into SP
                        vm->registers[i] = cpu_stack_pop(vm);
                    } else {
                        // Skip SP
                        vm->registers[R2_SP] += 4;
                    }
                }
            }
            break;
            
        case ENTER_OP:
            // Create stack frame
            cpu_enter_frame(vm, instr->immediate);
            break;
            
        case LEAVE_OP:
            // Destroy stack frame
            cpu_leave_frame(vm);
            break;
            
        default:
            // Unimplemented stack instruction
            vm->last_error = VM_ERROR_INVALID_INSTRUCTION;
            snprintf(vm->error_message, sizeof(vm->error_message), 
                     "Unimplemented stack instruction: 0x%02X", opcode);
            return VM_ERROR_INVALID_INSTRUCTION;
    }
    
    return VM_ERROR_NONE;
}

// Handle system instructions
static int handle_system(VM *vm, Instruction *instr) {
    uint8_t opcode = instr->opcode;
    
    switch (opcode) {
        case HALT_OP:
            // Halt VM execution
            vm->halted = 1;
            break;
            
        case INT_OP:
            // Generate software interrupt
            // TODO
            {
                uint8_t vector = instr->immediate & 0xFF;
                
                // TODO: Implement interrupt handling
                vm->last_error = VM_ERROR_UNHANDLED_INTERRUPT;
                snprintf(vm->error_message, sizeof(vm->error_message), 
                         "Unhandled interrupt: %d", vector);
                return VM_ERROR_UNHANDLED_INTERRUPT;
            }
            break;
            
        case CLI_OP:
            // Clear interrupt flag
            cpu_set_flag(vm, INT_FLAG, 0);
            break;
            
        case STI_OP:
            // Set interrupt flag
            cpu_set_flag(vm, INT_FLAG, 1);
            break;
            
        case IRET_OP:
            // Return from interrupt
            // TODO
            
            // Pop flags
            vm->registers[R4_SR] = cpu_stack_pop(vm);
            
            // Pop return address
            vm->registers[R3_PC] = cpu_stack_pop(vm);
            break;
            
        case IN_OP:
            // Input from I/O port
            {
                uint16_t port = instr->immediate;
                int value = 0;
                
                // Special handling for console input (port 0)
                if (port == 0) {
                    value = getchar();
                    if (value == EOF) {
                        value = 0;
                    }
                }
                
                vm->registers[instr->reg1] = value;
            }
            break;
            
        case OUT_OP:
            // Output to I/O port
            {
                uint16_t port = instr->reg1;
                uint32_t value;
                
                if (instr->mode == IMM_MODE) {
                    value = instr->immediate;
                } else {
                    value = vm->registers[instr->reg2];
                }
                
                // Special handling for console output (port 0)
                if (port == 0) {
                    putchar((int)(value & 0xFF));
                }
            }
            break;
            
        case CPUID_OP:
            // Get CPU information
            // CPUID TODO
            vm->registers[R0_ACC] = 0x00010001;  // Version 1.1
            break;
            
        case RESET_OP:
            // Reset VM
            cpu_reset(vm);
            break;
            
        case DEBUG_OP:
            // Trigger debugger
            vm->debug_mode = 1;
            break;
            
        default:
            // Unimplemented system instruction
            vm->last_error = VM_ERROR_INVALID_INSTRUCTION;
            snprintf(vm->error_message, sizeof(vm->error_message), 
                     "Unimplemented system instruction: 0x%02X", opcode);
            return VM_ERROR_INVALID_INSTRUCTION;
    }
    
    return VM_ERROR_NONE;
}

// Handle memory management instructions
static int handle_memory(VM *vm, Instruction *instr) {
    uint8_t opcode = instr->opcode;
    uint8_t reg = instr->reg1;
    uint16_t size, src, dst;
    uint8_t value;
    
    switch (opcode) {
        case ALLOC_OP:
            // Allocate heap memory
            size = get_operand_value(vm, instr, 1);
            vm->registers[reg] = memory_allocate(vm, size);
            break;
            
        case FREE_OP:
            // Free heap memory
            memory_free(vm, vm->registers[reg]);
            break;
            
        case MEMCPY_OP:
            // Copy memory block
            dst = vm->registers[reg];
            src = vm->registers[instr->reg2];
            size = instr->immediate;
            memory_copy(vm, dst, src, size);
            break;
            
        case MEMSET_OP:
            // Set memory block to a value
            dst = vm->registers[reg];
            value = vm->registers[instr->reg2] & 0xFF;
            size = instr->immediate;
            memory_set(vm, dst, value, size);
            break;
            
        case PROTECT_OP:
            // Set memory protection flags
            // To be implemented
            break;
            
        default:
            // Unimplemented memory management instruction
            vm->last_error = VM_ERROR_INVALID_INSTRUCTION;
            snprintf(vm->error_message, sizeof(vm->error_message), 
                     "Unimplemented memory management instruction: 0x%02X", opcode);
            return VM_ERROR_INVALID_INSTRUCTION;
    }
    
    return VM_ERROR_NONE;
}