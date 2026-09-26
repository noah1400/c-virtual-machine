#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "memory.h"
#include "syscalls.h"
#include "vm.h"

// Turns a fault raised while servicing a syscall into a status code in R5
static void syscall_status(VM *vm) {
    vm->registers[R5] = vm->last_error;
    vm_clear_error(vm);
}

static int invalid_syscall(VM *vm, uint16_t syscall_num) {
    vm->last_error = VM_ERROR_INVALID_SYSCALL;
    snprintf(vm->error_message, sizeof(vm->error_message),
             "Invalid system call: %d", syscall_num);
    return VM_ERROR_INVALID_SYSCALL;
}

static uint32_t mix_seed(uint32_t seed) {
    seed += 0x9E3779B9;
    seed = (seed ^ (seed >> 16)) * 0x85EBCA6B;
    seed = (seed ^ (seed >> 13)) * 0xC2B2AE35;
    seed ^= seed >> 16;
    return seed ? seed : VM_RNG_DEFAULT_SEED;
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
int syscall_dispatch(VM *vm, uint16_t syscall_num) {
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
                    // Interpret param1 as signed 16.16 fixed-point
                    int negative = (param1 & 0x80000000) != 0;
                    uint32_t magnitude = negative ? 0u - param1 : param1;
                    uint32_t decimal = ((magnitude & 0xFFFF) * 10000u) >> 16;

                    printf("%s%u.%04u", negative ? "-" : "", magnitude >> 16, decimal);
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
                return invalid_syscall(vm, syscall_num);
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
                return invalid_syscall(vm, syscall_num);
        }
    }
    else if (syscall_num < 30) {
        // Group 20-29: Memory operations
        switch (syscall_num) {
            case 20:  // Allocate memory (param1=size)
                vm->registers[R0_ACC] = memory_allocate(vm, param1);
                syscall_status(vm);
                break;

            case 21:  // Free memory (param1=address)
                memory_free(vm, (uint16_t)param1);
                vm->registers[R0_ACC] = vm->last_error;
                syscall_status(vm);
                break;

            case 22:  // Copy memory (param1=dest, param2=src, param3=count)
                {
                    uint16_t count = param3;
                    int result = memory_copy(vm, param1, param2, count);
                    vm->registers[R0_ACC] = result == VM_ERROR_NONE ? count : 0;
                    syscall_status(vm);
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
                return invalid_syscall(vm, syscall_num);
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
                    #ifdef _WIN32
                    Sleep(param1);
                    #else
                    struct timespec ts;
                    ts.tv_sec = param1 / 1000;
                    ts.tv_nsec = (long)(param1 % 1000) * 1000000L;
                    nanosleep(&ts, NULL);
                    #endif
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
                return invalid_syscall(vm, syscall_num);
        }
    }
    else if (syscall_num < 50) {
        // Group 40-49: Random number generation and misc
        switch (syscall_num) {
            case 40:  // Get random number in [0, max), full range when max is 0
                {
                    uint32_t x = vm->rng_state;
                    x ^= x << 13;
                    x ^= x >> 17;
                    x ^= x << 5;
                    vm->rng_state = x;

                    vm->registers[R0_ACC] = param1 ? (uint32_t)(((uint64_t)x * param1) >> 32) : x;
                    vm->registers[R5] = 0;
                }
                break;

            case 41:  // Seed random number generator (param1=seed)
                vm->rng_state = mix_seed(param1);
                vm->registers[R0_ACC] = 0;
                vm->registers[R5] = 0;
                break;

            default:
                return invalid_syscall(vm, syscall_num);
        }
    }
    else {
        return invalid_syscall(vm, syscall_num);
    }
    
    return VM_ERROR_NONE;
}
