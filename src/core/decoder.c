#include <stdio.h>
#include "vm_types.h"
#include "instruction_set.h"
#include "memory.h"
#include "decoder.h"

// Decode a 32-bit instruction at the specified memory address
int vm_decode_instruction(VM *vm, uint16_t address, Instruction *instr) {
    if (!vm || !instr) {
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    // Check if address is valid
    if (memory_check_address(vm, address, 4) != VM_ERROR_NONE) {
        return VM_ERROR_SEGMENTATION_FAULT;
    }
    
    isa_decode(memory_read_dword(vm, address), instr);

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

// Disassemble an instruction into human-readable text (for debugging)
void vm_disassemble_instruction(VM *vm, Instruction *instr, char *buffer, size_t buffer_size) {
    if (!vm || !instr || !buffer) {
        return;
    }
    
    const char *mnemonic = vm_opcode_to_mnemonic(instr->opcode);

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
    const InstructionInfo *info = isa_by_opcode(opcode);
    return info ? info->mnemonic : "UNKNOWN";
}
