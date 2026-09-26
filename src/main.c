#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm.h"
#include <stdbool.h>
#include "disassembler.h"
#include "cpu.h"
#include "memory.h"
#include <ctype.h>
#include "debug.h"
#include "debugger.h"

// Default memory size for VM
#define DEFAULT_MEMORY_SIZE (64 * 1024)  // 64KB

void print_usage(const char *program_name) {
    printf("Usage: %s [options] [program_file]\n", program_name);
    printf("Options:\n");
    printf("  -m SIZE       Set memory size in KB, at least 64 (default: 64)\n");
    printf("  -d            Enable debug mode\n");
    printf("  -dd           Enable extra verbose debug mode\n");
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
                        char *end;
                        long size = strtol(argv[i + 1], &end, 10);
                        if (*end != '\0' || size < 64 || size > 65536) {
                            fprintf(stderr, "Error: Memory size must be between 64 and 65536 KB\n");
                            return 0;
                        }
                        *memory_size = (int)(size * 1024);  // Convert KB to bytes
                        i++;
                    } else {
                        fprintf(stderr, "Error: Missing memory size value\n");
                        return 0;
                    }
                    break;
                    
                case 'd':
                    // Debug mode
                    *debug_mode = 1;
                    if (argv[i][2] == 'd') {
                        *debug_mode = 2;
                    }
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
        debugger_run(&vm);
    } else {
        // Run until halted
        printf("Running program...\n");
        result = vm_run(&vm);
        if (result != VM_ERROR_NONE) {
            fprintf(stderr, "VM error: %s\n", vm_get_error_message(&vm));
            fprintf(stderr, "Program terminated after %u instructions\n", vm.instruction_count);
            
            // Use the saved error PC
            uint32_t error_pc = vm.error_pc;
            
            // Decode and display the instruction
            Instruction instr;
            if (vm_peek_instruction(&vm, error_pc, &instr)) {
                char disasm[256];
                disasm_format(&instr, vm.debug_info, disasm, sizeof(disasm));
                fprintf(stderr, "Error occurred at PC=0x%04X, instruction: %s\n", error_pc, disasm);
            }
            
            vm_cleanup(&vm);
            return 1;
        }
        
        printf("Program completed after %u instructions\n", vm.instruction_count);
    }
    
    // Clean up
    vm_cleanup(&vm);
    
    return 0;
}