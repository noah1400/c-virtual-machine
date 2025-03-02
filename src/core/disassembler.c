#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm_types.h"
#include "instruction_set.h"
#include "decoder.h"
#include "memory.h"

// Print register name with optional suffix
void print_register(uint8_t reg, int with_suffix) {
    switch (reg) {
        case R0_ACC:
            printf("R0%s", with_suffix ? "(ACC)" : "");
            break;
        case R1_BP:
            printf("R1%s", with_suffix ? "(BP)" : "");
            break;
        case R2_SP:
            printf("R2%s", with_suffix ? "(SP)" : "");
            break;
        case R3_PC:
            printf("R3%s", with_suffix ? "(PC)" : "");
            break;
        case R4_SR:
            printf("R4%s", with_suffix ? "(SR)" : "");
            break;
        case R15_LR:
            printf("R15%s", with_suffix ? "(LR)" : "");
            break;
        default:
            printf("R%d", reg);
            break;
    }
}

// Disassemble a single instruction
void disassemble_instruction(uint32_t address, uint32_t instruction) {
    // Extract instruction fields
    uint8_t opcode = (instruction >> 24) & 0xFF;
    uint8_t mode = (instruction >> 20) & 0x0F;
    uint8_t reg1 = (instruction >> 16) & 0x0F;
    uint8_t reg2 = (instruction >> 12) & 0x0F;
    uint16_t immediate = instruction & 0x0FFF;

    // if mode is IMM_MODE, immediate value is reg2 combined with immediate
    // So we can use reg2 as the high bits of the immediate value so we have a 16 bit immediate value
    if (mode == IMM_MODE || mode == STK_MODE || mode == BAS_MODE || mode == MEM_MODE) {
        immediate |= (reg2 << 12);
    }
    
    // Get mnemonic
    const char *mnemonic = vm_opcode_to_mnemonic(opcode);
    
    // Print address and raw instruction
    printf("%04X:  %08X  ", address, instruction);
    
    // Print mnemonic
    printf("%-7s ", mnemonic);
    
    // Handle different instruction formats based on opcode
    switch (opcode) {
        // Instructions with no operands
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
            
        // Simple one-register instructions
        case INC_OP:
        case DEC_OP:
        case NEG_OP:
        case NOT_OP:
        case POP_OP:
            print_register(reg1, 1);
            break;
            
        // Return with optional stack adjustment
        case RET_OP:
            if (immediate > 0) {
                printf("0x%04X", immediate);
            }
            break;
            
        // Push immediate or register
        case PUSH_OP:
            if (mode == IMM_MODE) {
                printf("0x%04X", immediate);
            } else {
                print_register(reg1, 1);
            }
            break;
            
        // Jump/call instructions
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
            if (mode == IMM_MODE) {
                printf("0x%04X", immediate);
            } else if (mode == REG_MODE) {
                print_register(reg1, 1);
            } else {
                printf("[");
                print_register(reg1, 0);
                if (immediate > 0) {
                    printf(" + 0x%03X", immediate);
                }
                printf("]");
            }
            break;
            
        // Enter, interrupt, syscall (immediate value)
        case ENTER_OP:
        case INT_OP:
        case SYSCALL_OP:
            printf("0x%04X", immediate);
            break;
            
        // I/O operations
        case IN_OP:
            print_register(reg1, 1);
            printf(", 0x%04X", immediate);
            break;
            
        case OUT_OP:
            printf("0x%03X, ", reg1);
            if (mode == IMM_MODE) {
                printf("0x%04X", immediate);
            } else {
                print_register(reg2, 1);
            }
            break;
            
        // Loop instruction
        case LOOP_OP:
            print_register(reg1, 1);
            printf(", 0x%04X", immediate);
            break;
            
        // Two-operand register-memory instructions
        case LOAD_OP:
        case LOADB_OP:
        case LOADW_OP:
        case LEA_OP:
        case ADD_OP:
        case SUB_OP:
        case MUL_OP:
        case DIV_OP:
        case MOD_OP:
        case AND_OP:
        case OR_OP:
        case XOR_OP:
        case SHL_OP:
        case SHR_OP:
        case SAR_OP:
        case ROL_OP:
        case ROR_OP:
        case TEST_OP:
        case CMP_OP:
        case ADDC_OP:
        case SUBC_OP:
            // First operand is always a register
            print_register(reg1, 0);
            printf(", ");
            
            // Second operand depends on addressing mode
            switch (mode) {
                case IMM_MODE:
                    printf("0x%04X", immediate);
                    break;
                    
                case REG_MODE:
                    print_register(reg2, 0);
                    break;
                    
                case MEM_MODE:
                    printf("[0x%04X]", immediate);
                    break;
                    
                case REGM_MODE:
                    printf("[");
                    print_register(reg2, 0);
                    printf("]");
                    break;
                    
                case IDX_MODE:
                    printf("[");
                    print_register(reg2, 0);
                    if (immediate > 0) {
                        printf(" + 0x%03X", immediate);
                    }
                    printf("]");
                    break;
                    
                case STK_MODE:
                    printf("[SP + 0x%04X]", immediate);
                    break;
                    
                case BAS_MODE:
                    printf("[BP + 0x%04X]", immediate);
                    break;
                    
                default:
                    printf("???");
                    break;
            }
            break;
            
        // Store instructions (register to memory)
        case STORE_OP:
        case STOREB_OP:
        case STOREW_OP:
            print_register(reg1, 0);
            printf(", ");
            
            // Destination depends on addressing mode
            switch (mode) {
                case MEM_MODE:
                    printf("[0x%03X]", immediate);
                    break;
                    
                case REGM_MODE:
                    printf("[");
                    print_register(reg2, 0);
                    printf("]");
                    break;
                    
                case IDX_MODE:
                    printf("[");
                    print_register(reg2, 0);
                    if (immediate > 0) {
                        printf(" + 0x%03X", immediate);
                    }
                    printf("]");
                    break;
                    
                case STK_MODE:
                    printf("[SP + 0x%04X]", immediate);
                    break;
                    
                case BAS_MODE:
                    printf("[BP + 0x%04X]", immediate);
                    break;
                    
                default:
                    printf("???");
                    break;
            }
            break;
            
        // Move instruction (register to register)
        case MOVE_OP:
            print_register(reg1, 0);
            printf(", ");
            print_register(reg2, 0);
            break;
            
        // Memory management instructions
        case ALLOC_OP:
        case FREE_OP:
        case MEMCPY_OP:
        case MEMSET_OP:
        case PROTECT_OP:
            // These would be handled similar to other instructions
            // based on their specific formats
            printf("<memory op>");
            break;
            
        default:
            printf("???");
            break;
    }
    
    printf("\n");
}

// Disassemble a section of memory
void disassemble_memory(uint8_t *memory, uint32_t start_addr, uint32_t length) {
    printf("Disassembly of VM binary:\n");
    printf("Address  Raw Instr.  Assembly\n");
    printf("-------- ----------  --------\n");
    
    uint32_t addr = start_addr;
    uint32_t end_addr = start_addr + length;
    
    // Only disassemble code segment
    if (end_addr > CODE_SEGMENT_SIZE) {
        end_addr = CODE_SEGMENT_SIZE;
    }
    
    while (addr < end_addr) {
        // Each instruction is 4 bytes (32 bits)
        if (addr + 4 > end_addr) {
            break;
        }
        
        // Read the instruction from memory (little endian)
        uint32_t instruction = memory[addr] |
                              (memory[addr + 1] << 8) |
                              (memory[addr + 2] << 16) |
                              (memory[addr + 3] << 24);
        
        // Disassemble the instruction
        disassemble_instruction(addr, instruction);
        
        // Move to next instruction
        addr += 4;
    }
}

// Load a binary file for disassembly
uint8_t* load_binary_file(const char *filename, uint32_t *size) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Error: Could not open file '%s'\n", filename);
        return NULL;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Allocate memory for file contents
    uint8_t *buffer = (uint8_t*)malloc(file_size);
    if (!buffer) {
        fprintf(stderr, "Error: Failed to allocate memory for file\n");
        fclose(file);
        return NULL;
    }
    
    // Read file into buffer
    size_t bytes_read = fread(buffer, 1, file_size, file);
    fclose(file);
    
    if (bytes_read != file_size) {
        fprintf(stderr, "Error: Failed to read file (read %zu of %ld bytes)\n", 
                bytes_read, file_size);
        free(buffer);
        return NULL;
    }
    
    *size = (uint32_t)file_size;
    return buffer;
}

// Main function for disassembler
int disassemble_file(const char *filename) {
    uint32_t file_size;
    uint8_t *buffer = load_binary_file(filename, &file_size);
    
    if (!buffer) {
        return 1;
    }
    
    // Disassemble the binary
    disassemble_memory(buffer, 0, file_size);
    
    // Free the buffer
    free(buffer);
    
    return 0;
}