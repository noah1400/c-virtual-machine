#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vm.h>
#include <decoder.h>
#include <stdbool.h>
#include "disassembler.h"
#include <ctype.h>
#include <debug.h>

Breakpoint breakpoints[MAX_BREAKPOINTS];
int breakpoint_count = 0;

// Default memory size for VM
#define DEFAULT_MEMORY_SIZE (64 * 1024)  // 64KB

void print_usage(const char *program_name) {
    printf("Usage: %s [options] [program_file]\n", program_name);
    printf("Options:\n");
    printf("  -m SIZE       Set memory size in KB (default: 64)\n");
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

// Print current location with source context
void debug_print_current_context(VM *vm) {
    if (!vm) {
        return;
    }
    
    uint32_t pc = vm->registers[R3_PC];
    
    // Basic address information
    printf("PC: 0x%04X", pc);
    
    // Find symbol if debug info available
    if (vm->debug_info) {
        Symbol *sym = find_symbol_by_address(vm, pc);
        if (sym) {
            uint32_t offset = pc - sym->address;
            if (offset == 0) {
                printf(" (%s)", sym->name);
            } else {
                printf(" (%s+0x%X)", sym->name, offset);
            }
        }
        
        // Find source line
        SourceLine *line = find_source_line_by_address(vm, pc);
        printf("line: %d %s %s", line->line_num, line->source, line->source_file);
        if (line) {
            // Extract filename from line's source_file
            char *filename = NULL;
            if (line->source_file) {
                // Get just the basename from the full path
                filename = strrchr(line->source_file, '/');
                if (filename) {
                    filename++; // Skip the '/'
                } else {
                    filename = strrchr(line->source_file, '\\');
                    if (filename) {
                        filename++; // Skip the '\'
                    } else {
                        filename = line->source_file;
                    }
                }
            }
            
            printf("\nLine %d", line->line_num);
            if (filename) {
                printf(" [%s]", filename);
            }
            printf(": %s", line->source);
            
            // Print a few lines of context
            int context_lines = 2;  // Show 2 lines before and after
            printf("\nSource context:\n");
            
            // Context lines array: [0,1] = before, [2] = current, [3,4] = after
            SourceLine *context_array[5] = {NULL};
            context_array[2] = line;  // Current line in the middle
            
            // Find lines from the same file with line numbers before and after
            if (line->source_file) {
                // Find context lines from the same file
                for (uint32_t i = 0; i < vm->debug_info->source_line_count; i++) {
                    SourceLine *curr = &vm->debug_info->source_lines[i];
                    
                    // Skip if from a different file
                    if (!curr->source_file || strcmp(curr->source_file, line->source_file) != 0) {
                        continue;
                    }
                    
                    // Skip .include directives for context
                    if (curr->source && strstr(curr->source, ".include") != NULL) {
                        continue;
                    }
                    
                    // Lines before current
                    if (curr->line_num < line->line_num) {
                        int slot = -1;
                        if (line->line_num - curr->line_num == 1) slot = 1;  // Immediately before
                        else if (line->line_num - curr->line_num == 2) slot = 0;  // Two lines before
                        
                        if (slot >= 0 && slot < context_lines) {
                            context_array[slot] = curr;
                        }
                    }
                    // Lines after current
                    else if (curr->line_num > line->line_num) {
                        int slot = -1;
                        if (curr->line_num - line->line_num == 1) slot = 3;  // Immediately after
                        else if (curr->line_num - line->line_num == 2) slot = 4;  // Two lines after
                        
                        if (slot >= context_lines + 1 && slot < 5) {
                            context_array[slot] = curr;
                        }
                    }
                }
            }
            
            // Display context lines before current
            for (int i = 0; i < context_lines; i++) {
                if (context_array[i]) {
                    printf("%4d: %s\n", context_array[i]->line_num, context_array[i]->source);
                }
            }
            
            // Current line with marker
            printf("%4d: %s ← CURRENT", line->line_num, line->source);
            if (filename) {
                printf(" [%s]", filename);
            }
            printf("\n");
            
            // Lines after current
            for (int i = context_lines + 1; i < 5; i++) {
                if (context_array[i]) {
                    printf("%4d: %s\n", context_array[i]->line_num, context_array[i]->source);
                }
            }
        }
    }
    
    printf("\n");
}

// List all symbols
void debug_list_symbols(VM *vm) {
    if (!vm || !vm->debug_info) {
        printf("No debug information available\n");
        return;
    }
    
    printf("Symbols (%d total):\n", vm->debug_info->symbol_count);
    printf("%-20s %-6s %-8s %s\n", "NAME", "TYPE", "ADDRESS", "LINE");
    printf("------------------------------------------------\n");
    
    for (uint32_t i = 0; i < vm->debug_info->symbol_count; i++) {
        Symbol *sym = &vm->debug_info->symbols[i];
        printf("%-20s %-6s 0x%04X   %d %s\n", 
               sym->name, 
               sym->type == 0 ? "CODE" : "DATA", 
               sym->address, 
               sym->line_num,
               sym->source_file ? sym->source_file : "");
    }
}


// Set a breakpoint by address or symbol name
bool debug_set_breakpoint(VM *vm, const char *location) {
    if (breakpoint_count >= MAX_BREAKPOINTS) {
        printf("Maximum number of breakpoints reached\n");
        return false;
    }
    
    uint32_t address = 0;
    bool found = false;
    
    // Check if location is a number
    if (location[0] == '0' && (location[1] == 'x' || location[1] == 'X')) {
        // Hex address
        address = strtoul(location, NULL, 16);
        found = true;
    } else if (isdigit(location[0])) {
        // Decimal address
        address = strtoul(location, NULL, 10);
        found = true;
    } else if (vm->debug_info) {
        // Try to find symbol by name
        for (uint32_t i = 0; i < vm->debug_info->symbol_count; i++) {
            if (strcmp(vm->debug_info->symbols[i].name, location) == 0) {
                address = vm->debug_info->symbols[i].address;
                found = true;
                break;
            }
        }
    }
    
    if (!found) {
        printf("Invalid breakpoint location: %s\n", location);
        return false;
    }
    
    // Add the breakpoint
    breakpoints[breakpoint_count].address = address;
    breakpoints[breakpoint_count].name = strdup(location);
    breakpoints[breakpoint_count].enabled = true;
    breakpoint_count++;
    
    printf("Breakpoint %d set at 0x%04X", breakpoint_count, address);
    
    // Show symbol if available
    if (vm->debug_info) {
        Symbol *sym = find_symbol_by_address(vm, address);
        if (sym) {
            uint32_t offset = address - sym->address;
            if (offset == 0) {
                printf(" (%s)", sym->name);
            } else {
                printf(" (%s+0x%X)", sym->name, offset);
            }
        }
    }
    
    printf("\n");
    return true;
}

// Check if an address has a breakpoint
bool debug_has_breakpoint(uint32_t address) {
    for (int i = 0; i < breakpoint_count; i++) {
        if (breakpoints[i].enabled && breakpoints[i].address == address) {
            return true;
        }
    }
    return false;
}

// List all breakpoints
void debug_list_breakpoints(VM *vm) {
    if (breakpoint_count == 0) {
        printf("No breakpoints set\n");
        return;
    }
    
    printf("Breakpoints:\n");
    printf("%-4s %-8s %-20s %s\n", "NUM", "ADDRESS", "LOCATION", "STATUS");
    printf("------------------------------------------------\n");
    
    for (int i = 0; i < breakpoint_count; i++) {
        printf("%-4d 0x%04X   %-20s %s\n", 
               i + 1, 
               breakpoints[i].address, 
               breakpoints[i].name, 
               breakpoints[i].enabled ? "enabled" : "disabled");
        
        // Show symbol if available
        if (vm->debug_info) {
            Symbol *sym = find_symbol_by_address(vm, breakpoints[i].address);
            if (sym && strcmp(sym->name, breakpoints[i].name) != 0) {
                uint32_t offset = breakpoints[i].address - sym->address;
                if (offset == 0) {
                    printf("    Symbol: %s\n", sym->name);
                } else {
                    printf("    Symbol: %s+0x%X\n", sym->name, offset);
                }
            }
            
            // Show source line if available
            SourceLine *line = find_source_line_by_address(vm, breakpoints[i].address);
            if (line) {
                printf("    Line %d: %s\n", line->line_num, line->source);
            }
        }
    }
}

// Debug mode: Step through execution with user input
void debug_execution(VM *vm) {
    char cmd[256];
    
    printf("Debug mode enabled. Type 'h' for help.\n");
    
    while (!vm->halted) {
        // Show current context
        debug_print_current_context(vm);
        
        // Show instruction
        Instruction instr;
        if (vm_decode_instruction(vm, vm->registers[R3_PC], &instr) == VM_ERROR_NONE) {
            char instr_text[256];
            vm_disassemble_instruction(vm, &instr, instr_text, sizeof(instr_text));
            printf("Next instruction: %s\n", instr_text);
        }
        
        // Get command
        printf("> ");
        if (fgets(cmd, sizeof(cmd), stdin) == NULL) {
            break;
        }
        
        // Remove newline
        size_t len = strlen(cmd);
        if (len > 0 && cmd[len-1] == '\n') {
            cmd[len-1] = '\0';
        }
        
        // Process command
        if (strncmp(cmd, "s", 1) == 0 || strncmp(cmd, "step", 4) == 0) {
            // Step N instructions
            int count = 1;
            sscanf(cmd + 1, "%d", &count);
            if (count <= 0) count = 1;
            
            for (int i = 0; i < count && !vm->halted; i++) {
                int result = vm_step(vm);
                if (result != VM_ERROR_NONE) {
                    printf("Error: %s\n", vm_get_error_message(vm));
                    break;
                }
                
                // Check for breakpoint
                if (i < count - 1 && debug_has_breakpoint(vm->registers[R3_PC])) {
                    printf("Breakpoint hit at 0x%04X\n", vm->registers[R3_PC]);
                    break;
                }
            }
            
            // Show register state
            cpu_dump_registers(vm);
        }
        else if (strncmp(cmd, "n", 1) == 0 || strncmp(cmd, "next", 4) == 0) {
            // Step one line (not instruction)
            if (vm->debug_info) {
                // Find current source line
                SourceLine *current = find_source_line_by_address(vm, vm->registers[R3_PC]);
                if (current) {
                    uint32_t current_line = current->line_num;
                    
                    // Execute until we reach a different line or a breakpoint
                    while (!vm->halted) {
                        int result = vm_step(vm);
                        if (result != VM_ERROR_NONE) {
                            printf("Error: %s\n", vm_get_error_message(vm));
                            break;
                        }
                        
                        // Check for breakpoint
                        if (debug_has_breakpoint(vm->registers[R3_PC])) {
                            printf("Breakpoint hit at 0x%04X\n", vm->registers[R3_PC]);
                            break;
                        }
                        
                        // Check if we're on a new line
                        SourceLine *new_line = find_source_line_by_address(vm, vm->registers[R3_PC]);
                        if (new_line && new_line->line_num != current_line) {
                            break;
                        }
                    }
                } else {
                    // No source line info, just step one instruction
                    vm_step(vm);
                }
            } else {
                // No debug info, just step one instruction
                vm_step(vm);
            }
            
            // Show register state
            cpu_dump_registers(vm);
        }
        else if (strncmp(cmd, "c", 1) == 0 || strncmp(cmd, "continue", 8) == 0) {
            // Run until breakpoint or halt
            while (!vm->halted) {
                int result = vm_step(vm);
                if (result != VM_ERROR_NONE) {
                    printf("Error: %s\n", vm_get_error_message(vm));
                    break;
                }
                
                // Check for breakpoint
                if (debug_has_breakpoint(vm->registers[R3_PC])) {
                    printf("Breakpoint hit at 0x%04X\n", vm->registers[R3_PC]);
                    break;
                }
            }
            
            if (vm->halted) {
                printf("Program halted\n");
            }
            
            // Show register state
            cpu_dump_registers(vm);
        }
        else if (strncmp(cmd, "b ", 2) == 0 || strncmp(cmd, "break ", 6) == 0) {
            // Set breakpoint
            char *location = cmd + (cmd[1] == ' ' ? 2 : 6);
            debug_set_breakpoint(vm, location);
        }
        else if (strcmp(cmd, "lb") == 0 || strcmp(cmd, "list-break") == 0) {
            // List breakpoints
            debug_list_breakpoints(vm);
        }
        else if (strcmp(cmd, "ls") == 0 || strcmp(cmd, "list-symbols") == 0) {
            // List symbols
            debug_list_symbols(vm);
        }
        else if (strncmp(cmd, "m ", 2) == 0 || strncmp(cmd, "memory ", 7) == 0) {
            // Dump memory
            unsigned addr = 0;
            int count = 16;
            
            if (cmd[1] == ' ') {
                sscanf(cmd + 2, "%x %d", &addr, &count);
            } else {
                sscanf(cmd + 7, "%x %d", &addr, &count);
            }
            
            debug_dump_memory(vm, addr, count);
        }
        else if (strcmp(cmd, "r") == 0 || strcmp(cmd, "registers") == 0) {
            // Show registers
            cpu_dump_registers(vm);
        }
        else if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
            // Quit
            break;
        }
        else if (strcmp(cmd, "h") == 0 || strcmp(cmd, "help") == 0) {
            // Help
            printf("Debug commands:\n");
            printf("  s, step [N]     - Step N instructions (default: 1)\n");
            printf("  n, next         - Step to next source line\n");
            printf("  c, continue     - Run until breakpoint or halt\n");
            printf("  b, break ADDR   - Set breakpoint at address or symbol\n");
            printf("  lb, list-break  - List all breakpoints\n");
            printf("  ls, list-symbols- List all symbols\n");
            printf("  m, memory ADDR N- Dump N bytes of memory at ADDR\n");
            printf("  r, registers    - Show register values\n");
            printf("  q, quit         - Quit debugger\n");
            printf("  h, help         - Show this help\n");
        }
        else if (cmd[0] != '\0') {
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
    debug_print_source_info(&vm);
    debug_dump_source_mapping(&vm);
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
            
            // Use the saved error PC
            uint16_t error_pc = vm.error_pc;
            
            // Decode and display the instruction
            Instruction instr;
            if (vm_decode_instruction(&vm, error_pc, &instr) == VM_ERROR_NONE) {
                char disasm[256];
                vm_disassemble_instruction(&vm, &instr, disasm, sizeof(disasm));
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