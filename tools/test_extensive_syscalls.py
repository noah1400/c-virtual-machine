#!/usr/bin/env python3
"""
Test Program for Extended Syscalls in VM

This script generates a binary test program that tests various syscalls 
implemented in the VM.
"""

import struct
import sys

# Addressing modes
IMM_MODE = 0x0   # Immediate: Value in instruction
REG_MODE = 0x1   # Register: Value in register
MEM_MODE = 0x2   # Memory: Direct address
REGM_MODE = 0x3  # Register Indirect: [Reg]
IDX_MODE = 0x4   # Indexed: [Reg + Offset]
STK_MODE = 0x5   # Stack Relative: [SP + Offset]
BAS_MODE = 0x6   # Base Relative: [BP + Offset]

# Register definitions
R0_ACC = 0   # Accumulator
R5 = 5       # General purpose register
R6 = 6       # General purpose register
R7 = 7       # General purpose register

# Opcodes
NOP_OP = 0x00      # No operation
LOAD_OP = 0x01     # Load value into register
STORE_OP = 0x02    # Store value to memory
MOVE_OP = 0x03     # Copy register to register
ADD_OP = 0x20      # Add to register
SUB_OP = 0x21      # Subtract from register
INC_OP = 0x25      # Increment register
DEC_OP = 0x26      # Decrement register
CMP_OP = 0x28      # Compare
JMP_OP = 0x60      # Unconditional jump
JZ_OP = 0x61       # Jump if zero
JNZ_OP = 0x62      # Jump if not zero
SYSCALL_OP = 0x6C  # System call
HALT_OP = 0xA0     # Halt execution

def encode_instruction(opcode, mode, reg1, reg2, immediate):
    """Encode a VM instruction into a 32-bit value."""
    # Ensure fields are within their bit limits
    opcode = opcode & 0xFF        # 8 bits
    mode = mode & 0x0F            # 4 bits
    reg1 = reg1 & 0x0F            # 4 bits
    reg2 = reg2 & 0x0F            # 4 bits
    immediate = immediate & 0xFFF  # 12 bits
    
    # Build the instruction
    instruction = (opcode << 24) | (mode << 20) | (reg1 << 16) | (reg2 << 12) | immediate
    return instruction

def add_print_string(program, string):
    """Add instructions to print a string."""
    # For each character in the string, print it
    for char in string:
        program.append(encode_instruction(LOAD_OP, IMM_MODE, R0_ACC, 0, ord(char)))
        program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 0))  # Print char

def add_print_newline(program):
    """Add instructions to print a newline."""
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R0_ACC, 0, 10))  # Newline ASCII
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 0))

def generate_syscall_test_program():
    """Generate a program to test extended syscalls."""
    program = []
    data_section = 0x4100  # Location in data segment for our data
    
    # Test header
    add_print_string(program, "Extended Syscall Test Program")
    add_print_newline(program)
    add_print_newline(program)
    
    # ----- CONSOLE I/O TESTS -----
    
    add_print_string(program, "Testing Console I/O Syscalls:")
    add_print_newline(program)
    
    # Test syscall 0: Print character
    add_print_string(program, "  Syscall 0 (Print char): ")
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R0_ACC, 0, 65))  # 'A'
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 0))
    add_print_newline(program)
    
    # Test syscall 1: Print integer
    add_print_string(program, "  Syscall 1 (Print int): ")
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R0_ACC, 0, 12345))
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 1))
    add_print_newline(program)
    
    # Test syscall 5: Print hex
    add_print_string(program, "  Syscall 5 (Print hex): ")
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R0_ACC, 0, 0xABCD))
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 5))
    add_print_newline(program)
    
    # Test syscall 6: Print in base
    add_print_string(program, "  Syscall 6 (Print base-2): ")
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R0_ACC, 0, 42))
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R5, 0, 2))  # Binary
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 6))
    add_print_newline(program)
    
    # Test syscall 7: Print float
    add_print_string(program, "  Syscall 7 (Print float): ")
    # Value 3.1415 in fixed point (16.16 format) = 3*65536 + 0.1415*65536
    fixed_val = int(3.1415 * 65536)
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R0_ACC, 0, fixed_val & 0xFFF))
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 7))
    add_print_newline(program)
    
    # ----- COLOR TEST WITH CAREFUL RESET -----
    
    # Add delimiter to clearly see before/after
    add_print_string(program, "  === Color Test Below ===")
    add_print_newline(program)
    
    # Set color to green
    add_print_string(program, "  ")  # Indentation
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R0_ACC, 0, 2))  # Green foreground
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 9))
    
    # Print green text
    add_print_string(program, "This text should be GREEN")
    
    # Immediately reset color (uses special code 0xFF)
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R0_ACC, 0, 0xFF))
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 9))
    
    add_print_newline(program)
    
    # Add delimiter to clearly see after-reset
    add_print_string(program, "  === Color Test Above ===")
    add_print_newline(program)
    
    # ----- MEMORY TESTS -----
    
    add_print_newline(program)
    add_print_string(program, "Testing Memory Syscalls:")
    add_print_newline(program)
    
    # Test syscall 20: Allocate memory
    add_print_string(program, "  Syscall 20 (Allocate): ")
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R0_ACC, 0, 100))  # 100 bytes
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 20))
    # Store allocated address in R7 for later use
    program.append(encode_instruction(MOVE_OP, REG_MODE, R7, R0_ACC, 0))
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 5))  # Print in hex
    add_print_newline(program)
    
    # Test syscall 22: Copy memory
    add_print_string(program, "  Syscall 22 (Copy memory): ")
    # Copy "Hello" to allocated memory
    # First store "Hello" in data segment
    hello_addr = data_section
    # H
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R0_ACC, 0, 72))  # 'H'
    program.append(encode_instruction(STORE_OP, MEM_MODE, R0_ACC, 0, hello_addr))
    # e
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R0_ACC, 0, 101))  # 'e'
    program.append(encode_instruction(STORE_OP, MEM_MODE, R0_ACC, 0, hello_addr + 1))
    # l
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R0_ACC, 0, 108))  # 'l'
    program.append(encode_instruction(STORE_OP, MEM_MODE, R0_ACC, 0, hello_addr + 2))
    # l
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R0_ACC, 0, 108))  # 'l'
    program.append(encode_instruction(STORE_OP, MEM_MODE, R0_ACC, 0, hello_addr + 3))
    # o
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R0_ACC, 0, 111))  # 'o'
    program.append(encode_instruction(STORE_OP, MEM_MODE, R0_ACC, 0, hello_addr + 4))
    # NULL
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R0_ACC, 0, 0))
    program.append(encode_instruction(STORE_OP, MEM_MODE, R0_ACC, 0, hello_addr + 5))
    
    # Now copy from data segment to heap
    program.append(encode_instruction(MOVE_OP, REG_MODE, R0_ACC, R7, 0))  # Destination
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R5, 0, hello_addr))  # Source
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R6, 0, 6))  # Count (including null)
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 22))
    
    # Print the string from heap to verify
    program.append(encode_instruction(MOVE_OP, REG_MODE, R0_ACC, R7, 0))
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 2))
    add_print_newline(program)
    
    # Test syscall 23: Memory information
    add_print_string(program, "  Syscall 23 (Memory info):")
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 23))
    add_print_newline(program)
    
    add_print_string(program, "    Total memory: ")
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 1))  # Print int
    add_print_string(program, " bytes")
    add_print_newline(program)
    
    # ----- RANDOM NUMBER TESTS -----
    
    add_print_newline(program)
    add_print_string(program, "Testing Random Number Syscalls:")
    add_print_newline(program)
    
    # Test syscall 41: Seed RNG
    add_print_string(program, "  Syscall 41 (Seed RNG): ")
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R0_ACC, 0, 12345))
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 41))
    add_print_string(program, "Seeded with 12345")
    add_print_newline(program)
    
    # Test syscall 40: Get random number
    add_print_string(program, "  Syscall 40 (Random numbers 0-100):")
    add_print_newline(program)
    
    # Generate a single random number
    add_print_string(program, "    Random: ")
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R0_ACC, 0, 100))  # Max 100
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 40))
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 1))  # Print int
    add_print_newline(program)
    
    # ----- PROCESS CONTROL TESTS -----
    
    add_print_newline(program)
    add_print_string(program, "Testing Process Control Syscalls:")
    add_print_newline(program)
    
    # Test syscall 32: Get system time
    add_print_string(program, "  Syscall 32 (System time): ")
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 32))
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 1))  # Print int
    add_print_string(program, " ms")
    add_print_newline(program)
    
    # Test syscall 33: Performance counter
    add_print_string(program, "  Syscall 33 (Performance counter): ")
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 33))
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 1))  # Print int
    add_print_string(program, " instructions")
    add_print_newline(program)
    
    # Test syscall 31: Sleep
    add_print_string(program, "  Syscall 31 (Sleep 1000ms): ")
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R0_ACC, 0, 1000))  # 1000ms
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 31))
    add_print_string(program, "Completed")
    add_print_newline(program)
    
    # Show performance counter again after sleep
    add_print_string(program, "  Performance counter after sleep: ")
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 33))
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 1))  # Print int
    add_print_string(program, " instructions")
    add_print_newline(program)
    
    # ----- FINISHING UP -----
    
    add_print_newline(program)
    add_print_string(program, "All syscall tests completed.")
    add_print_newline(program)
    
    # Test syscall 21: Free memory
    add_print_string(program, "Freeing allocated memory...")
    program.append(encode_instruction(MOVE_OP, REG_MODE, R0_ACC, R7, 0))
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 21))
    add_print_newline(program)
    
    # Test syscall 30: Exit program
    add_print_string(program, "Exiting with code 42 in 2 seconds...")
    add_print_newline(program)
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R0_ACC, 0, 2000))  # 2000ms
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 31))
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R0_ACC, 0, 42))
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 30))
    
    # HALT instruction (shouldn't be reached due to exit syscall)
    program.append(encode_instruction(HALT_OP, IMM_MODE, 0, 0, 0))
    
    # Convert instructions to binary
    binary_program = bytearray()
    for instr in program:
        binary_program.extend(struct.pack("<I", instr))
    
    return binary_program

def main():
    """Main function to generate and output the test program."""
    binary_program = generate_syscall_test_program()
    
    # Write to stdout in binary mode
    if hasattr(sys.stdout, 'buffer'):
        sys.stdout.buffer.write(binary_program)
    else:
        sys.stdout.write(binary_program)

if __name__ == "__main__":
    main()