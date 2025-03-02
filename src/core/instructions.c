#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/**
 * Handle system calls
 * 
 * Syscall conventions:
 * - Syscall number is in immediate field of instruction
 * - Parameters are passed in registers R0_ACC, R5, R6, R7
 * - Return value is placed in R0_ACC
 * - Error code is placed in R5 (0 = success)
 * 
 * Returns VM_ERROR_NONE on success or appropriate error code on failure
 */
static int handle_syscall(VM *vm, uint16_t syscall_num) {
    // Input parameters
    uint32_t param1 = vm->registers[R0_ACC]; // First parameter
    uint32_t param2 = vm->registers[R5];     // Second parameter
    uint32_t param3 = vm->registers[R6];     // Third parameter
    uint32_t param4 = vm->registers[R7];     // Fourth parameter

    vm->registers[R5] = 0;  // Clear error code
    
    // Categorize syscalls by functional group
    if (syscall_num < 10) {
        // Group 0-9: Basic console I/O
        switch (syscall_num) {
            case 0:  // Print character
                printf("%c", (char)param1);
                fflush(stdout);
                break;
                
            case 1:  // Print integer (decimal)
                printf("%d", (int)param1);
                fflush(stdout);
                break;
                
            case 2:  // Print string
                {
                    uint16_t addr = param1;
                    char c;
                    
                    while ((c = memory_read_byte(vm, addr)) != 0) {
                        printf("%c", c);
                        addr++;
                    }
                    fflush(stdout);
                }
                break;
                
            case 3:  // Read character
                {
                    int c = getchar();
                    vm->registers[R0_ACC] = (c == EOF) ? 0 : c;
                }
                break;
                
            case 4:  // Read string (up to param2 chars)
                {
                    uint16_t addr = param1;
                    uint16_t max_len = param2;
                    uint16_t i = 0;
                    int c;
                    
                    if (max_len == 0) {
                        vm->registers[R0_ACC] = 0; // No characters read
                        vm->registers[R5] = 1;
                        break;
                    }
                    
                    // Reserve space for null terminator
                    max_len--;
                    
                    // Read input string
                    while (i < max_len) {
                        c = getchar();
                        
                        if (c == EOF || c == '\n') {
                            break;
                        }
                        
                        memory_write_byte(vm, addr + i, (uint8_t)c);
                        i++;
                    }
                    
                    // Add null terminator
                    memory_write_byte(vm, addr + i, 0);
                    
                    // Return number of characters read
                    vm->registers[R0_ACC] = i;
                }
                break;
                
            case 5:  // Print integer (hexadecimal)
                printf("0x%x", (unsigned int)param1);
                fflush(stdout);
                break;
                
            case 6:  // Print formatted integer with base (param2 = base)
                {
                    unsigned int value = param1;
                    unsigned int base = param2;
                    
                    // Validate base (2-36)
                    if (base < 2 || base > 36) {
                        base = 10;
                    }
                    
                    // Simple base conversion (for a more complete implementation, 
                    // you'd want to handle negative numbers specially)
                    char buffer[33];  // Enough for 32-bit number in any base
                    char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
                    int pos = 0;
                    
                    // Special case for 0
                    if (value == 0) {
                        printf("0");
                        break;
                    }
                    
                    // Convert number to string in specified base
                    while (value > 0 && pos < 32) {
                        buffer[pos++] = digits[value % base];
                        value /= base;
                    }
                    
                    // Print in reverse order
                    while (pos > 0) {
                        putchar(buffer[--pos]);
                    }
                    fflush(stdout);
                }
                break;
                
            case 7:  // Print floating point (emulated using fixed-point)
                {
                    // Interpret param1 as fixed-point (16.16 format)
                    int32_t fixed_val = (int32_t)param1;
                    int32_t integer_part = fixed_val >> 16;
                    uint32_t frac_part = fixed_val & 0xFFFF;
                    
                    // Convert fraction to decimal
                    // (multiply by 10000 and divide by 2^16)
                    uint32_t decimal = (frac_part * 10000) >> 16;
                    
                    printf("%d.%04u", integer_part, decimal);
                    fflush(stdout);
                }
                break;
                
            case 8:  // Control console - clear screen
                printf("\033[2J\033[H"); // ANSI escape sequence to clear screen and move cursor to home
                fflush(stdout);
                break;
                
            case 9:  // Control console - set color
                {
                    uint8_t fg = param1 & 0xFF;
                    uint8_t bg = (param1 >> 8) & 0xFF;
                    
                    // Make sure everything is flushed before changing colors
                    fflush(stdout);
                    
                    if (fg == 0xFF) {
                        // Special case for reset - force to default colors
                        // Use the most complete reset sequence possible
                        printf("\033[0;39;49m");  // Reset all attributes and explicitly set default colors
                    } else if (fg < 8) {
                        // ANSI color codes (0-7 standard colors)
                        if (bg < 8) {
                            // Set both foreground and background
                            printf("\033[0;%d;%dm", 30 + fg, 40 + bg);
                        } else {
                            // Set only foreground
                            printf("\033[0;%dm", 30 + fg);
                        }
                    }
                    fflush(stdout);
                }
                break;
                
            default:
                vm->registers[R5] = 1;  // Error - unimplemented
                break;
        }
    }
    else if (syscall_num < 20) {
        // Group 10-19: File operations (simplified)
        switch (syscall_num) {
            case 10:  // File open (param1=filename addr, param2=mode)
                {
                    // Extract filename
                    uint16_t addr = param1;
                    uint8_t mode = param2 & 0xFF;
                    char filename[256] = {0};
                    int i = 0;
                    char c;
                    
                    // Copy filename from VM memory
                    while (i < 255 && (c = memory_read_byte(vm, addr + i)) != 0) {
                        filename[i++] = c;
                    }
                    filename[i] = 0;
                    
                    // Map mode to file open mode
                    char file_mode[4] = {0};
                    switch (mode) {
                        case 0: strcpy(file_mode, "r"); break;   // Read
                        case 1: strcpy(file_mode, "w"); break;   // Write
                        case 2: strcpy(file_mode, "a"); break;   // Append
                        case 3: strcpy(file_mode, "r+"); break;  // Read/Write
                        default: strcpy(file_mode, "r"); break;  // Default to read
                    }
                    
                    // We would need to maintain a file table in the VM for real file I/O
                    // For now, just simulate by returning a dummy file handle
                    vm->registers[R0_ACC] = 1;  // Dummy file handle
                    vm->registers[R5] = 0;      // Success
                }
                break;
                
            case 11:  // File close (param1=file handle)
                {
                    // Here we would free resources associated with the file handle
                    // For this simplified implementation, just return success
                    vm->registers[R0_ACC] = 0;  // Success
                    vm->registers[R5] = 0;
                }
                break;
                
            case 12:  // File read (param1=file handle, param2=buffer addr, param3=count)
                {
                    // Simplified implementation - always return some dummy data
                    uint16_t buffer_addr = param2;
                    uint16_t count = param3;
                    
                    // Make sure we don't exceed memory bounds
                    if (buffer_addr + count > vm->memory_size) {
                        count = vm->memory_size - buffer_addr;
                    }
                    
                    // Fill buffer with sequential values
                    for (uint16_t i = 0; i < count; i++) {
                        memory_write_byte(vm, buffer_addr + i, i & 0xFF);
                    }
                    
                    // Return number of bytes read
                    vm->registers[R0_ACC] = count;
                    vm->registers[R5] = 0;  // Success
                }
                break;
                
            case 13:  // File write (param1=file handle, param2=buffer addr, param3=count)
                {
                    // Simplified implementation - just pretend we wrote the data
                    uint16_t count = param3;
                    
                    // Return number of bytes written
                    vm->registers[R0_ACC] = count;
                    vm->registers[R5] = 0;  // Success
                }
                break;
                
            default:
                vm->registers[R5] = 1;  // Error - unimplemented
                break;
        }
    }
    else if (syscall_num < 30) {
        // Group 20-29: Memory operations
        switch (syscall_num) {
            case 20:  // Allocate memory (param1=size)
                {
                    uint16_t size = param1;
                    uint16_t addr = memory_allocate(vm, size);
                    
                    vm->registers[R0_ACC] = addr;  // Return address
                    vm->registers[R5] = (addr == 0) ? 1 : 0;  // Error if allocation failed
                }
                break;
                
            case 21:  // Free memory (param1=address)
                {
                    uint16_t addr = param1;
                    int result = memory_free(vm, addr);
                    
                    vm->registers[R0_ACC] = result;
                    vm->registers[R5] = (result == VM_ERROR_NONE) ? 0 : 1;
                }
                break;
                
            case 22:  // Copy memory (param1=dest, param2=src, param3=count)
                {
                    uint16_t dest = param1;
                    uint16_t src = param2;
                    uint16_t count = param3;
                    
                    int result = memory_copy(vm, dest, src, count);
                    
                    vm->registers[R0_ACC] = count;  // Return bytes copied
                    vm->registers[R5] = (result == VM_ERROR_NONE) ? 0 : 1;
                }
                break;
                
            case 23:  // Memory information
                {
                    // Return total memory size
                    vm->registers[R0_ACC] = vm->memory_size;
                    
                    // Return segment boundaries and sizes
                    vm->registers[R5] = (CODE_SEGMENT_BASE << 16) | CODE_SEGMENT_SIZE;
                    vm->registers[R6] = (DATA_SEGMENT_BASE << 16) | DATA_SEGMENT_SIZE;
                    vm->registers[R7] = (STACK_SEGMENT_BASE << 16) | STACK_SEGMENT_SIZE;
                    
                    vm->registers[R5] = 0;  // Success
                }
                break;
                
            default:
                vm->registers[R5] = 1;  // Error - unimplemented
                break;
        }
    }
    else if (syscall_num < 40) {
        // Group 30-39: Process control
        switch (syscall_num) {
            case 30:  // Exit program with return code (param1=code)
                {
                    // Set return code (for potential host program)
                    vm->registers[R0_ACC] = param1;
                    
                    // Halt the VM
                    vm->halted = 1;
                }
                break;
                
            case 31:  // Sleep (param1=milliseconds)
                {
                    // Use platform-specific sleep
                    #ifdef _WIN32
                    // Windows
                    Sleep(param1);  // Windows Sleep takes milliseconds
                    #else
                    // Unix/Linux/macOS
                    struct timespec ts;
                    ts.tv_sec = param1 / 1000;
                    ts.tv_nsec = (param1 % 1000) * 1000000;
                    nanosleep(&ts, NULL);
                    #endif
                    
                    // Still update instruction count for consistency
                    uint32_t dummy_cycles = param1 / 10;
                    if (dummy_cycles > 0) {
                        vm->instruction_count += dummy_cycles;
                    } else {
                        vm->instruction_count++;
                    }
                }
                break;
                
            case 32:  // Get system time (milliseconds since VM start)
                {
                    // Simulate a system time based on instruction count
                    // In a real implementation, you'd use the host system's clock
                    vm->registers[R0_ACC] = vm->instruction_count * 10;  // Rough estimate
                    vm->registers[R5] = 0;  // Success
                }
                break;
                
            case 33:  // Get performance counter (high resolution)
                {
                    // Just return exact instruction count
                    vm->registers[R0_ACC] = vm->instruction_count;
                    vm->registers[R5] = 0;  // Success
                }
                break;
                
            default:
                vm->registers[R5] = 1;  // Error - unimplemented
                break;
        }
    }
    else if (syscall_num < 50) {
        // Group 40-49: Random number generation and misc
        switch (syscall_num) {
            case 40:  // Get random number (param1=max value)
                {
                    uint32_t max_val = param1;
                    
                    if (max_val == 0) {
                        max_val = 0xFFFFFFFF;  // Full 32-bit range
                    }
                    
                    // Simple pseudo-random number generation
                    // In a real implementation, you'd use a better PRNG
                    static uint32_t seed = 0x12345678;
                    seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF;
                    
                    // Return random number in range [0, max_val]
                    vm->registers[R0_ACC] = (uint32_t)((uint64_t)seed * max_val / 0x7FFFFFFF);
                    vm->registers[R5] = 0;  // Success
                }
                break;
                
            case 41:  // Seed random number generator (param1=seed)
                {
                    // Set the seed for the PRNG
                    uint32_t seed_val = param1;
                    
                    // In a real implementation, this would set the PRNG seed
                    // For this simplified version, just store and return success
                    static uint32_t seed = 0x12345678;
                    seed = seed_val;
                    
                    vm->registers[R0_ACC] = 0;  // Success
                    vm->registers[R5] = 0;
                }
                break;
                
            default:
                vm->registers[R5] = 1;  // Error - unimplemented
                break;
        }
    }
    else {
        // Unknown syscall group
        vm->registers[R5] = 1;  // Error code
        return VM_ERROR_INVALID_SYSCALL;
    }
    
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
            {
                uint16_t syscall_num = instr->immediate;
                int result = handle_syscall(vm, syscall_num);
                         
                if (result != VM_ERROR_NONE) {
                        vm->last_error = result;
                    snprintf(vm->error_message, sizeof(vm->error_message),
                        "Invalid system call: %d", syscall_num);
                    return result;
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
            {
                uint16_t vector = instr->immediate;
                cpu_interrupt(vm, vector);
            }
            break;
            
        case CLI_OP:
            // Clear interrupt flag
            cpu_disable_interrupts(vm);
            break;
            
        case STI_OP:
            // Set interrupt flag
            cpu_enable_interrupts(vm);
            break;
            
        case IRET_OP:
            cpu_return_from_interrupt(vm);
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
            // CPUID works similar to x86 - function number in R0_ACC determines what information to return
            {
                uint32_t function = vm->registers[R0_ACC];
                
                switch (function) {
                    case 0: // Basic vendor info and maximum supported function
                        // Return maximum function number in R0_ACC
                        vm->registers[R0_ACC] = 4;
                        
                        // Store vendor string in R5-R7 ("VM32CPU" in ASCII)
                        vm->registers[R5] = 0x334D5632; // "2VM3"
                        vm->registers[R6] = 0x55504332; // "2CPU"
                        vm->registers[R7] = 0x00000000; // Null terminator
                        break;
                        
                    case 1: // Version and feature information
                        // R0_ACC: Version information
                        // Format: [Major:8][Minor:8][Revision:8][Reserved:8]
                        vm->registers[R0_ACC] = 0x00010001; // Version 1.1.0
                        
                        // R5: Feature flags 1
                        vm->registers[R5] = 0x00000001 |  // Bit 0: Has FPU emulation  (not implemented yet)
                                            0x00000002 |  // Bit 1: Has SIMD           (not implemented yet)
                                            0x00000004 |  // Bit 2: Has I/O system
                                            0x00000008 |  // Bit 3: Has memory protection (partially implemented)
                                            0x00000010 |  // Bit 4: Has interrupts      (partially implemented)
                                            0x00000020;   // Bit 5: Has syscalls
                        
                        // R6: Feature flags 2
                        vm->registers[R6] = 0x00000001 |  // Bit 0: Has debug support
                                            0x00000002;   // Bit 1: Has timer device
                        
                        // R7: Reserved for future use
                        vm->registers[R7] = 0;
                        break;
                        
                    case 2: // Memory information
                        // R0_ACC: Total memory size in bytes
                        vm->registers[R0_ACC] = vm->memory_size;
                        
                        // R5: Segment information (base addresses)
                        vm->registers[R5] = (CODE_SEGMENT_BASE << 24) | 
                                           (DATA_SEGMENT_BASE << 16) | 
                                           (STACK_SEGMENT_BASE << 8) | 
                                           (HEAP_SEGMENT_BASE);
                        
                        // R6: Segment information (sizes in KB)
                        vm->registers[R6] = (CODE_SEGMENT_SIZE / 1024 << 24) | 
                                           (DATA_SEGMENT_SIZE / 1024 << 16) | 
                                           (STACK_SEGMENT_SIZE / 1024 << 8) | 
                                           (HEAP_SEGMENT_SIZE / 1024);
                        
                        // R7: Reserved for future use
                        vm->registers[R7] = 0;
                        break;
                        
                    case 3: // Instruction set information
                        // R0_ACC: Total number of defined opcodes
                        vm->registers[R0_ACC] = 0xE0; // Approx 224 opcodes (up to 0xDF)
                        
                        // R5: Supported addressing modes bit mask (1 bit per mode)
                        vm->registers[R5] = (1 << IMM_MODE) | 
                                           (1 << REG_MODE) | 
                                           (1 << MEM_MODE) | 
                                           (1 << REGM_MODE) | 
                                           (1 << IDX_MODE) | 
                                           (1 << STK_MODE) | 
                                           (1 << BAS_MODE);
                        
                        // R6: Implemented instruction groups (bit field)
                        vm->registers[R6] = 0x0000007F; // All 7 instruction groups implemented
                        
                        // R7: Reserved for future extensions
                        vm->registers[R7] = 0;
                        break;
                        
                    case 4: // VM information and status
                        // R0_ACC: Instruction count
                        vm->registers[R0_ACC] = vm->instruction_count;
                        
                        // R5: VM state flags
                        vm->registers[R5] = (vm->halted ? 0x01 : 0) |
                                           (vm->debug_mode ? 0x02 : 0) |
                                           (vm->interrupt_enabled ? 0x04 : 0);
                        
                        // R6: Last error code
                        vm->registers[R6] = vm->last_error;
                        
                        // R7: Reserved
                        vm->registers[R7] = 0;
                        break;
                        
                    default: // Unsupported function, return zeros
                        vm->registers[R0_ACC] = 0;
                        vm->registers[R5] = 0;
                        vm->registers[R6] = 0;
                        vm->registers[R7] = 0;
                        break;
                }
            }
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