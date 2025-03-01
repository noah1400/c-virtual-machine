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
    
    // Check if this memory region might contain a string
    if (memory_might_be_string(vm, addr)) {
        char* str = memory_extract_string(vm, addr, 80);
        if (str) {
            printf("\nPossible string at 0x%04X: \"%s\"\n", addr, str);
            free(str);
        }
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
            
        } else if (cmd[0] == 'h' || cmd[0] == 'H' || cmd[0] == '?') {
            // Help
            printf("Debug commands:\n");
            printf("  s [N]    - Step N instructions (default: 1)\n");
            printf("  r        - Run until halt\n");
            printf("  q        - Quit\n");
            printf("  m ADDR N - Dump N bytes of memory at ADDR\n");
            printf("  h        - Show this help\n");
            
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
            vm_cleanup(&vm);
            return 1;
        }
        
        printf("Program completed after %u instructions\n", vm.instruction_count);
    }
    
    // Clean up
    vm_cleanup(&vm);
    
    return 0;
}