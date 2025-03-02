#include <stdio.h>
#include "vm_types.h"
#include "instruction_set.h"
#include "memory.h"

// Decode a 32-bit instruction at the specified memory address
int vm_decode_instruction(VM *vm, uint16_t address, Instruction *instr) {
    if (!vm || !instr) {
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    // Check if address is valid
    if (memory_check_address(vm, address, 4) != VM_ERROR_NONE) {
        return VM_ERROR_SEGMENTATION_FAULT;
    }
    
    // Read the 32-bit instruction from memory
    uint32_t raw_instruction = memory_read_dword(vm, address);
    
    // Decode the instruction fields
    // Format: [Opcode: 8 bits][Mode: 4 bits][Reg1: 4 bits][Reg2: 4 bits][Immediate/Offset: 12 bits]
    instr->opcode = (uint8_t)((raw_instruction >> 24) & 0xFF);
    instr->mode = (uint8_t)((raw_instruction >> 20) & 0x0F);
    instr->reg1 = (uint8_t)((raw_instruction >> 16) & 0x0F);
    instr->reg2 = (uint8_t)((raw_instruction >> 12) & 0x0F);
    instr->immediate = (uint16_t)(raw_instruction & 0x0FFF);

    // if mode is IMM_MODE, immediate value is reg2 combined with immediate
    if (instr->mode == IMM_MODE) {
        instr->immediate |= (instr->reg2 << 12);
    }
    
    return VM_ERROR_NONE;
}

// Fetch and decode the instruction at the program counter
uint32_t vm_fetch_instruction(VM *vm) {
    if (!vm) {
        return 0;
    }
    
    // Get program counter
    uint16_t pc = vm->registers[R3_PC];
    
    // Read the instruction from memory
    return memory_read_dword(vm, pc);
}

// Encode an instruction structure back to binary format
uint32_t vm_encode_instruction(Instruction *instr) {
    if (!instr) {
        return 0;
    }
    
    // Construct the 32-bit instruction
    // Format: [Opcode: 8 bits][Mode: 4 bits][Reg1: 4 bits][Reg2: 4 bits][Immediate/Offset: 12 bits]
    uint32_t encoded = 0;
    encoded |= ((uint32_t)instr->opcode) << 24;
    encoded |= ((uint32_t)instr->mode & 0x0F) << 20;
    encoded |= ((uint32_t)instr->reg1 & 0x0F) << 16;
    encoded |= ((uint32_t)instr->reg2 & 0x0F) << 12;
    encoded |= ((uint32_t)instr->immediate & 0x0FFF);
    
    return encoded;
}

// Disassemble an instruction into human-readable text (for debugging)
void vm_disassemble_instruction(VM *vm, Instruction *instr, char *buffer, size_t buffer_size) {
    if (!vm || !instr || !buffer) {
        return;
    }
    
    // Get instruction mnemonic based on opcode
    const char *mnemonic = "UNKNOWN";
    
    switch (instr->opcode) {
        case NOP_OP:     mnemonic = "NOP"; break;
        case LOAD_OP:    mnemonic = "LOAD"; break;
        case STORE_OP:   mnemonic = "STORE"; break;
        case MOVE_OP:    mnemonic = "MOVE"; break;
        case LOADB_OP:   mnemonic = "LOADB"; break;
        case STOREB_OP:  mnemonic = "STOREB"; break;
        case LOADW_OP:   mnemonic = "LOADW"; break;
        case STOREW_OP:  mnemonic = "STOREW"; break;
        case LEA_OP:     mnemonic = "LEA"; break;
        case ADD_OP:     mnemonic = "ADD"; break;
        case SUB_OP:     mnemonic = "SUB"; break;
        case MUL_OP:     mnemonic = "MUL"; break;
        case DIV_OP:     mnemonic = "DIV"; break;
        case MOD_OP:     mnemonic = "MOD"; break;
        case INC_OP:     mnemonic = "INC"; break;
        case DEC_OP:     mnemonic = "DEC"; break;
        case NEG_OP:     mnemonic = "NEG"; break;
        case CMP_OP:     mnemonic = "CMP"; break;
        case ADDC_OP:    mnemonic = "ADDC"; break;
        case SUBC_OP:    mnemonic = "SUBC"; break;
        case AND_OP:     mnemonic = "AND"; break;
        case OR_OP:      mnemonic = "OR"; break;
        case XOR_OP:     mnemonic = "XOR"; break;
        case NOT_OP:     mnemonic = "NOT"; break;
        case SHL_OP:     mnemonic = "SHL"; break;
        case SHR_OP:     mnemonic = "SHR"; break;
        case SAR_OP:     mnemonic = "SAR"; break;
        case ROL_OP:     mnemonic = "ROL"; break;
        case ROR_OP:     mnemonic = "ROR"; break;
        case TEST_OP:    mnemonic = "TEST"; break;
        case JMP_OP:     mnemonic = "JMP"; break;
        case JZ_OP:      mnemonic = "JZ"; break;
        case JNZ_OP:     mnemonic = "JNZ"; break;
        case JN_OP:      mnemonic = "JN"; break;
        case JP_OP:      mnemonic = "JP"; break;
        case JO_OP:      mnemonic = "JO"; break;
        case JC_OP:      mnemonic = "JC"; break;
        case JBE_OP:     mnemonic = "JBE"; break;
        case JA_OP:      mnemonic = "JA"; break;
        case CALL_OP:    mnemonic = "CALL"; break;
        case RET_OP:     mnemonic = "RET"; break;
        case SYSCALL_OP: mnemonic = "SYSCALL"; break;
        case LOOP_OP:    mnemonic = "LOOP"; break;
        case PUSH_OP:    mnemonic = "PUSH"; break;
        case POP_OP:     mnemonic = "POP"; break;
        case PUSHF_OP:   mnemonic = "PUSHF"; break;
        case POPF_OP:    mnemonic = "POPF"; break;
        case PUSHA_OP:   mnemonic = "PUSHA"; break;
        case POPA_OP:    mnemonic = "POPA"; break;
        case ENTER_OP:   mnemonic = "ENTER"; break;
        case LEAVE_OP:   mnemonic = "LEAVE"; break;
        case HALT_OP:    mnemonic = "HALT"; break;
        case INT_OP:     mnemonic = "INT"; break;
        case CLI_OP:     mnemonic = "CLI"; break;
        case STI_OP:     mnemonic = "STI"; break;
        case IRET_OP:    mnemonic = "IRET"; break;
        case IN_OP:      mnemonic = "IN"; break;
        case OUT_OP:     mnemonic = "OUT"; break;
        case CPUID_OP:   mnemonic = "CPUID"; break;
        case RESET_OP:   mnemonic = "RESET"; break;
        case DEBUG_OP:   mnemonic = "DEBUG"; break;
        case ALLOC_OP:   mnemonic = "ALLOC"; break;
        case FREE_OP:    mnemonic = "FREE"; break;
        case MEMCPY_OP:  mnemonic = "MEMCPY"; break;
        case MEMSET_OP:  mnemonic = "MEMSET"; break;
        case PROTECT_OP: mnemonic = "PROTECT"; break;
    }
    
    // Format operands based on addressing mode
    char operands[128] = "";
    
    switch (instr->opcode) {
        case NOP_OP:
        case PUSHF_OP:
        case POPF_OP:
        case PUSHA_OP:
        case POPA_OP:
        case LEAVE_OP:
        case HALT_OP:
        case CLI_OP:
        case STI_OP:
        case IRET_OP:
        case CPUID_OP:
        case RESET_OP:
        case DEBUG_OP:
            // No operands
            break;
            
        case RET_OP:
            // Optional immediate value for stack adjustment
            if (instr->immediate > 0) {
                snprintf(operands, sizeof(operands), "0x%03X", instr->immediate);
            }
            break;
            
        case INC_OP:
        case DEC_OP:
        case NEG_OP:
        case NOT_OP:
        case POP_OP:
            // Single register operand
            snprintf(operands, sizeof(operands), "R%d", instr->reg1);
            break;
            
        case PUSH_OP:
            // Register or immediate
            if (instr->mode == IMM_MODE) {
                snprintf(operands, sizeof(operands), "0x%03X", instr->immediate);
            } else {
                snprintf(operands, sizeof(operands), "R%d", instr->reg1);
            }
            break;
            
        case JMP_OP:
        case JZ_OP:
        case JNZ_OP:
        case JN_OP:
        case JP_OP:
        case JO_OP:
        case JC_OP:
        case JBE_OP:
        case JA_OP:
        case CALL_OP:
            // Target address or register
            if (instr->mode == IMM_MODE) {
                snprintf(operands, sizeof(operands), "0x%03X", instr->immediate);
            } else if (instr->mode == REG_MODE) {
                snprintf(operands, sizeof(operands), "R%d", instr->reg1);
            } else {
                snprintf(operands, sizeof(operands), "[R%d + 0x%03X]", 
                         instr->reg1, instr->immediate);
            }
            break;
            
        case ENTER_OP:
        case INT_OP:
        case SYSCALL_OP:
            // Immediate value
            snprintf(operands, sizeof(operands), "0x%03X", instr->immediate);
            break;
            
        case IN_OP:
            // Reg, Port
            snprintf(operands, sizeof(operands), "R%d, 0x%03X", 
                     instr->reg1, instr->immediate);
            break;
            
        case OUT_OP:
            // Port, Reg/Imm
            if (instr->mode == IMM_MODE) {
                snprintf(operands, sizeof(operands), "0x%03X, 0x%03X", 
                         instr->reg1, instr->immediate);
            } else {
                snprintf(operands, sizeof(operands), "0x%03X, R%d", 
                         instr->reg1, instr->reg2);
            }
            break;
            
        case LOOP_OP:
            // Reg, Target
            snprintf(operands, sizeof(operands), "R%d, 0x%03X", 
                     instr->reg1, instr->immediate);
            break;
            
        default:
            // More complex operands depend on addressing mode
            switch (instr->mode) {
                case IMM_MODE:
                    snprintf(operands, sizeof(operands), "R%d, 0x%03X", 
                             instr->reg1, instr->immediate);
                    break;
                case REG_MODE:
                    snprintf(operands, sizeof(operands), "R%d, R%d", 
                             instr->reg1, instr->reg2);
                    break;
                case MEM_MODE:
                    snprintf(operands, sizeof(operands), "R%d, [0x%03X]", 
                             instr->reg1, instr->immediate);
                    break;
                case REGM_MODE:
                    snprintf(operands, sizeof(operands), "R%d, [R%d]", 
                             instr->reg1, instr->reg2);
                    break;
                case IDX_MODE:
                    snprintf(operands, sizeof(operands), "R%d, [R%d + 0x%03X]", 
                             instr->reg1, instr->reg2, instr->immediate);
                    break;
                case STK_MODE:
                    snprintf(operands, sizeof(operands), "R%d, [SP + 0x%03X]", 
                             instr->reg1, instr->immediate);
                    break;
                case BAS_MODE:
                    snprintf(operands, sizeof(operands), "R%d, [BP + 0x%03X]", 
                             instr->reg1, instr->immediate);
                    break;
            }
    }
    
    // Format the full instruction
    if (operands[0] == '\0') {
        snprintf(buffer, buffer_size, "%s", mnemonic);
    } else {
        snprintf(buffer, buffer_size, "%s %s", mnemonic, operands);
    }
}

const char* vm_opcode_to_mnemonic(uint8_t opcode) {
    switch (opcode) {
        case NOP_OP:     return "NOP";
        case LOAD_OP:    return "LOAD";
        case STORE_OP:   return "STORE";
        case MOVE_OP:    return "MOVE";
        case LOADB_OP:   return "LOADB";
        case STOREB_OP:  return "STOREB";
        case LOADW_OP:   return "LOADW";
        case STOREW_OP:  return "STOREW";
        case LEA_OP:     return "LEA";
        case ADD_OP:     return "ADD";
        case SUB_OP:     return "SUB";
        case MUL_OP:     return "MUL";
        case DIV_OP:     return "DIV";
        case MOD_OP:     return "MOD";
        case INC_OP:     return "INC";
        case DEC_OP:     return "DEC";
        case NEG_OP:     return "NEG";
        case CMP_OP:     return "CMP";
        case ADDC_OP:    return "ADDC";
        case SUBC_OP:    return "SUBC";
        case AND_OP:     return "AND";
        case OR_OP:      return "OR";
        case XOR_OP:     return "XOR";
        case NOT_OP:     return "NOT";
        case SHL_OP:     return "SHL";
        case SHR_OP:     return "SHR";
        case SAR_OP:     return "SAR";
        case ROL_OP:     return "ROL";
        case ROR_OP:     return "ROR";
        case TEST_OP:    return "TEST";
        case JMP_OP:     return "JMP";
        case JZ_OP:      return "JZ";
        case JNZ_OP:     return "JNZ";
        case JN_OP:      return "JN";
        case JP_OP:      return "JP";
        case JO_OP:      return "JO";
        case JC_OP:      return "JC";
        case JBE_OP:     return "JBE";
        case JA_OP:      return "JA";
        case CALL_OP:    return "CALL";
        case RET_OP:     return "RET";
        case SYSCALL_OP: return "SYSCALL";
        case LOOP_OP:    return "LOOP";
        case PUSH_OP:    return "PUSH";
        case POP_OP:     return "POP";
        case PUSHF_OP:   return "PUSHF";
        case POPF_OP:    return "POPF";
        case PUSHA_OP:   return "PUSHA";
        case POPA_OP:    return "POPA";
        case ENTER_OP:   return "ENTER";
        case LEAVE_OP:   return "LEAVE";
        case HALT_OP:    return "HALT";
        case INT_OP:     return "INT";
        case CLI_OP:     return "CLI";
        case STI_OP:     return "STI";
        case IRET_OP:    return "IRET";
        case IN_OP:      return "IN";
        case OUT_OP:     return "OUT";
        case CPUID_OP:   return "CPUID";
        case RESET_OP:   return "RESET";
        case DEBUG_OP:   return "DEBUG";
        case ALLOC_OP:   return "ALLOC";
        case FREE_OP:    return "FREE";
        case MEMCPY_OP:  return "MEMCPY";
        case MEMSET_OP:  return "MEMSET";
        case PROTECT_OP: return "PROTECT";
        default:         return "UNKNOWN";
    }
}