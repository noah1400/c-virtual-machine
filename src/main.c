#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vm.h>
#include <decoder.h>
#include "disassembler.h"

// Default memory size for VM
#define DEFAULT_MEMORY_SIZE (64 * 1024)  // 64KB

void print_usage(const char *program_name) {
    printf("Usage: %s [options] [program_file]\n", program_name);
    printf("Options:\n");
    printf("  -m SIZE       Set memory size in KB (default: 64)\n");
    printf("  -d            Enable debug mode\n");
    printf("  -D            Disassemble program file instead of running it\n");
    printf("  -h            Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s program.bin         Run program.bin with default settings\n", program_name);
    printf("  %s -m 128 program.bin  Run with 128KB memory\n", program_name);
    printf("  %s -d program.bin      Run in debug mode\n", program_name);
    printf("  %s -D program.bin      Disassemble program.bin\n", program_name);
}

// Parse command line arguments
int parse_arguments(int argc, char *argv[], int *memory_size, int *debug_mode, 
    int *disassemble_mode, char **program_file) {
    int i;

    // Set defaults
    *memory_size = DEFAULT_MEMORY_SIZE;
    *debug_mode = 0;
    *disassemble_mode = 0;
    *program_file = NULL;

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
        // Option
            switch (argv[i][1]) {
                case 'm':
                    // Memory size
                    if (i + 1 < argc) {
                        int size = atoi(argv[i + 1]);
                        if (size <= 0) {
                            fprintf(stderr, "Error: Invalid memory size\n");
                            return 0;
                        }
                        *memory_size = size * 1024;  // Convert KB to bytes
                        i++;
                    } else {
                        fprintf(stderr, "Error: Missing memory size value\n");
                        return 0;
                    }
                    break;
                    
                case 'd':
                    // Debug mode
                    *debug_mode = 1;
                    break;
                    
                case 'D':
                    // Disassemble mode
                    *disassemble_mode = 1;
                    break;
                    
                case 'h':
                    // Help
                    print_usage(argv[0]);
                    return 0;
                    
                default:
                    fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
                    print_usage(argv[0]);
                    return 0;
            }
        } else {
        // Program file
            if (*program_file == NULL) {
                *program_file = argv[i];
            } else {
                fprintf(stderr, "Error: Multiple program files specified\n");
                return 0;
            }
        }
    }

    return 1;
}

void debug_dump_memory(VM *vm, uint16_t addr, int count) {
    if (!vm || addr >= vm->memory_size) {
        printf("Error: Address out of range\n");
        return;
    }
    
    if (count <= 0) {
        count = 16;
    }
    
    if (addr + count > vm->memory_size) {
        count = vm->memory_size - addr;
    }
    
    printf("Memory dump at 0x%04X:\n", addr);
    
    // Display memory in hex format
    for (int i = 0; i < count; i++) {
        if (i % 16 == 0) {
            printf("0x%04X: ", addr + i);
        }
        
        printf("%02X ", vm->memory[addr + i]);
        
        if (i % 16 == 15 || i == count - 1) {
            // If at end of line, print ASCII representation
            int padding = 16 - (i % 16 + 1);
            for (int p = 0; p < padding; p++) {
                printf("   ");
            }
            
            printf(" | ");
            
            // Print ASCII representation
            int start = i - (i % 16);
            int end = (i % 16 == 15) ? i : i;
            for (int j = start; j <= end; j++) {
                char c = vm->memory[addr + j];
                if (c >= 32 && c <= 126) {
                    printf("%c", c);
                } else {
                    printf(".");
                }
            }
            
            printf("\n");
        }
    }
}

void debug_decode_instruction(VM *vm, uint16_t address) {
    // Read the 32-bit instruction from memory
    uint32_t instruction = memory_read_dword(vm, address);
    
    // Decode fields
    uint8_t opcode = (instruction >> 24) & 0xFF;
    uint8_t mode = (instruction >> 20) & 0x0F;
    uint8_t reg1 = (instruction >> 16) & 0x0F;
    uint8_t reg2 = (instruction >> 12) & 0x0F;
    uint16_t immediate = instruction & 0xFFF;
    
    // If mode is immediate-related, combine reg2 and immediate
    if (mode == IMM_MODE || mode == MEM_MODE || mode == STK_MODE || mode == BAS_MODE) {
        immediate = (reg2 << 12) | immediate;
    }
    
    // Get opcode name
    const char* opcode_name = "UNKNOWN";
    switch (opcode) {
        case NOP_OP: opcode_name = "NOP"; break;
        case LOAD_OP: opcode_name = "LOAD"; break;
        case STORE_OP: opcode_name = "STORE"; break;
        case MOVE_OP: opcode_name = "MOVE"; break;
        case LOADB_OP: opcode_name = "LOADB"; break;
        case STOREB_OP: opcode_name = "STOREB"; break;
        case ADD_OP: opcode_name = "ADD"; break;
        case SUB_OP: opcode_name = "SUB"; break;
        case MUL_OP: opcode_name = "MUL"; break;
        case DIV_OP: opcode_name = "DIV"; break;
        case INC_OP: opcode_name = "INC"; break;
        case DEC_OP: opcode_name = "DEC"; break;
        case JMP_OP: opcode_name = "JMP"; break;
        case JZ_OP: opcode_name = "JZ"; break;
        case JNZ_OP: opcode_name = "JNZ"; break;
        case SYSCALL_OP: opcode_name = "SYSCALL"; break;
        case PUSH_OP: opcode_name = "PUSH"; break;
        case POP_OP: opcode_name = "POP"; break;
        case HALT_OP: opcode_name = "HALT"; break;
        case ALLOC_OP: opcode_name = "ALLOC"; break;
        case FREE_OP: opcode_name = "FREE"; break;
        case MEMCPY_OP: opcode_name = "MEMCPY"; break;
        case MEMSET_OP: opcode_name = "MEMSET"; break;
        case PROTECT_OP: opcode_name = "PROTECT"; break;
        default: opcode_name = "UNKNOWN"; break;
    }
    
    // Get mode name
    const char* mode_name = "UNKNOWN";
    switch (mode) {
        case IMM_MODE: mode_name = "IMM"; break;
        case REG_MODE: mode_name = "REG"; break;
        case MEM_MODE: mode_name = "MEM"; break;
        case REGM_MODE: mode_name = "REGM"; break;
        case IDX_MODE: mode_name = "IDX"; break;
        case STK_MODE: mode_name = "STK"; break;
        case BAS_MODE: mode_name = "BAS"; break;
        default: mode_name = "UNKNOWN"; break;
    }
    
    printf("INSTRUCTION 0x%08X at 0x%04X:\n", instruction, address);
    printf("  Opcode: 0x%02X (%s)\n", opcode, opcode_name);
    printf("  Mode: 0x%01X (%s)\n", mode, mode_name);
    printf("  Reg1: %d (R%d)\n", reg1, reg1);
    printf("  Reg2: %d (R%d)\n", reg2, reg2);
    printf("  Immediate: 0x%04X (%d)\n", immediate, immediate);
    
    // Try to interpret the instruction
    printf("  Interpretation: ");
    
    if (opcode == ALLOC_OP) {
        if (mode == REG_MODE) {
            printf("ALLOC R%d, R%d (Allocate memory with size from R%d, store address in R%d)\n",
                  reg1, reg2, reg2, reg1);
        } else if (mode == IMM_MODE) {
            printf("ALLOC R%d, #%d (Allocate %d bytes, store address in R%d)\n",
                  reg1, immediate, immediate, reg1);
        } else {
            printf("ALLOC with unknown mode\n");
        }
    } else if (opcode == FREE_OP) {
        printf("FREE R%d (Free memory at address in R%d)\n", reg1, reg1);
    } else {
        printf("%s instruction\n", opcode_name);
    }
}

// Debug mode: Step through execution with user input
void debug_execution(VM *vm) {
    char cmd[32];
    int step_count = 1;
    
    printf("Debug mode enabled. Commands:\n");
    printf("  s [N]    - Step N instructions (default: 1)\n");
    printf("  r        - Run until halt\n");
    printf("  q        - Quit\n");
    printf("  m ADDR N - Dump N bytes of memory at ADDR\n");
    printf("  h        - Show this help\n");
    printf("  b        - Run until breakpoint\n");
    printf("  i        - Decode instruction at current PC\n");
    
    while (!vm->halted) {
        // Show current state
        printf("\nPC: 0x%04X  Instruction count: %u\n", vm->registers[R3_PC], vm->instruction_count);
        
        // Get next instruction
        Instruction instr;
        if (vm_decode_instruction(vm, vm->registers[R3_PC], &instr) == VM_ERROR_NONE) {
            // Try to disassemble
            const char* mnemonic = vm_opcode_to_mnemonic(instr.opcode);
            printf("Next: OP=0x%02X (%s) MODE=0x%01X R1=0x%01X R2=0x%01X IMM=0x%03X\n",
                   instr.opcode, mnemonic, instr.mode, instr.reg1, instr.reg2, instr.immediate);
        }
        
        // Get user command
        printf("> ");
        if (fgets(cmd, sizeof(cmd), stdin) == NULL) {
            break;
        }
        
        // Process command
        if (cmd[0] == 's' || cmd[0] == 'S') {
            // Step N instructions
            step_count = 1;
            if (strlen(cmd) > 1) {
                step_count = atoi(cmd + 1);
                if (step_count <= 0) {
                    step_count = 1;
                }
            }
            
            for (int i = 0; i < step_count && !vm->halted; i++) {
                int result = vm_step(vm);
                if (result != VM_ERROR_NONE) {
                    printf("Error: %s\n", vm_get_error_message(vm));
                    break;
                }
            }
            
            // Show register state
            cpu_dump_registers(vm);
            
        } else if (cmd[0] == 'r' || cmd[0] == 'R') {
            // Run until halt
            int result = vm_run(vm);
            if (result != VM_ERROR_NONE) {
                printf("Error: %s\n", vm_get_error_message(vm));
            }
            
            printf("VM halted after %u instructions\n", vm->instruction_count);
            cpu_dump_registers(vm);
            break;
            
        } else if (cmd[0] == 'q' || cmd[0] == 'Q') {
            // Quit
            break;
            
        } else if (cmd[0] == 'm' || cmd[0] == 'M') {
            // Dump memory
            unsigned int addr = 0;
            int count = 16;
            if (sscanf(cmd + 1, "%x %d", &addr, &count) >= 1) {
                debug_dump_memory(vm, (uint16_t)addr, count);
            } else {
                printf("Error: Invalid memory dump command\n");
            }

        } else if (cmd[0] == 'b' || cmd[0] == 'B') {
            // set debug to 0
            vm->debug_mode = 0;
            // run til debug_mode is 1
            while (!vm->halted && !vm->debug_mode) {
                int result = vm_step(vm);
                if (result != VM_ERROR_NONE) {
                    printf("Error: %s\n", vm_get_error_message(vm));
                    break;
                }
            }

            cpu_dump_registers(vm);
            
        } else if (cmd[0] == 'h' || cmd[0] == 'H' || cmd[0] == '?') {
            // Help
            printf("Debug commands:\n");
            printf("  s [N]    - Step N instructions (default: 1)\n");
            printf("  r        - Run until halt\n");
            printf("  q        - Quit\n");
            printf("  m ADDR N - Dump N bytes of memory at ADDR\n");
            printf("  h        - Show this help\n");
            printf("  b        - Run until breakpoint\n");
            printf("  i        - Decode instruction at current PC\n");

        } else if (cmd[0] == 'i' || cmd[0] == 'I') {
            // Decode instruction
            unsigned int addr = vm->registers[R3_PC];
            if (strlen(cmd) > 1) {
                sscanf(cmd + 1, "%x", &addr);
            }
            debug_decode_instruction(vm, addr);
        } else {
            // Unknown command
            printf("Unknown command. Type 'h' for help.\n");
        }
    }
}

// Main function
int main(int argc, char *argv[]) {
    int memory_size;
    int debug_mode;
    int disassemble_mode;
    char *program_file;
    VM vm;
    int result;
    
    // Parse command line arguments
    if (!parse_arguments(argc, argv, &memory_size, &debug_mode, &disassemble_mode, &program_file)) {
        return 1;
    }
    
    // Check if program file is specified
    if (program_file == NULL) {
        fprintf(stderr, "Error: No program file specified\n");
        print_usage(argv[0]);
        return 1;
    }
    
    // Handle disassemble mode
    if (disassemble_mode) {
        printf("Disassembling '%s'...\n", program_file);
        return disassemble_file(program_file);
    }
    
    // Initialize VM
    printf("Initializing VM with %d KB memory...\n", memory_size / 1024);
    result = vm_init(&vm, memory_size);
    if (result != VM_ERROR_NONE) {
        fprintf(stderr, "Failed to initialize VM: %s\n", vm_get_error_string(result));
        return 1;
    }
    
    // Set debug mode if requested
    vm.debug_mode = debug_mode;
    
    // Load program
    printf("Loading program '%s'...\n", program_file);
    result = vm_load_program_file(&vm, program_file);
    if (result != VM_ERROR_NONE) {
        fprintf(stderr, "Failed to load program: %s\n", vm_get_error_message(&vm));
        vm_cleanup(&vm);
        return 1;
    }
    
    printf("Program loaded, starting at 0x%04X\n", vm.registers[R3_PC]);
    
    // Execute program
    if (debug_mode) {
        // Run in debug mode
        debug_execution(&vm);
    } else {
        // Run until halted
        printf("Running program...\n");
        result = vm_run(&vm);
        if (result != VM_ERROR_NONE) {
            fprintf(stderr, "VM error: %s\n", vm_get_error_message(&vm));
            fprintf(stderr, "Program terminated after %u instructions\n", vm.instruction_count);
            // decode instruction
            Instruction instr;
            vm_decode_instruction(&vm, vm.registers[R3_PC]-4, &instr);
            const char* mnemonic = vm_opcode_to_mnemonic(instr.opcode);
            printf("Next: OP=0x%02X (%s) MODE=0x%01X R1=0x%01X R2=0x%01X IMM=0x%03X\n",
                   instr.opcode, mnemonic, instr.mode, instr.reg1, instr.reg2, instr.immediate);
            vm_cleanup(&vm);
            return 1;
        }
        
        printf("Program completed after %u instructions\n", vm.instruction_count);
    }
    
    // Clean up
    vm_cleanup(&vm);
    
    return 0;
}