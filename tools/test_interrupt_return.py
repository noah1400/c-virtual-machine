#!/usr/bin/env python3
"""
Test Program for Interrupt Handling in VM

This script generates a binary test program that demonstrates
basic interrupt handling and the IRET instruction.
"""

import struct
import sys

# Addressing modes
IMM_MODE = 0x0   # Immediate: Value in instruction
REG_MODE = 0x1   # Register: Value in register
MEM_MODE = 0x2   # Memory: Direct address

# Register definitions
R0_ACC = 0   # Accumulator
R4_SR = 4    # Status register
R5 = 5       # General purpose register
R6 = 6       # General purpose register
R7 = 7       # General purpose register
R8 = 8       # General purpose register

# Opcodes
NOP_OP = 0x00      # No operation
LOAD_OP = 0x01     # Load value into register
STORE_OP = 0x02    # Store value to memory
MOVE_OP = 0x03     # Copy register to register
JMP_OP = 0x60      # Unconditional jump
SYSCALL_OP = 0x6C  # System call
INT_OP = 0xA1      # Generate interrupt
STI_OP = 0xA3      # Set interrupt flag
IRET_OP = 0xA4     # Return from interrupt
HALT_OP = 0xA0     # Halt execution
DEBUG_OP = 0xA9
SHL_OP = 0x44
OR_OP = 0x41

# Constants
VECTOR_TABLE_BASE = 0x0100
INTERRUPT_HANDLER_BASE = 0x1000
INT_FLAG = 0x10    # Interrupt enable flag

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

def generate_interrupt_test_program():
    """Generate a program to test interrupt handling."""
    # We'll create a program with:
    # 1. Main program code at 0x0000
    # 2. Interrupt vector table at 0x0100
    # 3. Interrupt handler at 0x1000
    
    # Initialize program with zeros to pad for vector table
    program = [0] * (VECTOR_TABLE_BASE // 4)  # 4 bytes per instruction
    
    # Start of main program (0x0000)
    main_program = []
    
    add_print_string(main_program, "Interrupt Test Program")
    add_print_newline(main_program)
    
    # Enable interrupts
    add_print_string(main_program, "Enabling interrupts...")
    add_print_newline(main_program)
    main_program.append(encode_instruction(STI_OP, IMM_MODE, 0, 0, 0))
    
    # Set up vector table for interrupt 0x10
    add_print_string(main_program, "Setting up interrupt vector...")
    add_print_newline(main_program)
    main_program.append(encode_instruction(LOAD_OP, IMM_MODE, R0_ACC, (INTERRUPT_HANDLER_BASE >> 12) & 0xF, INTERRUPT_HANDLER_BASE & 0xFFF))
    main_program.append(encode_instruction(STORE_OP, MEM_MODE, R0_ACC, 0, VECTOR_TABLE_BASE + (0x10 * 4)))
    # Store some test values in registers
    add_print_string(main_program, "Storing values in registers...")
    add_print_newline(main_program)
    main_program.append(encode_instruction(LOAD_OP, IMM_MODE, R5, 0, 0x55))
    main_program.append(encode_instruction(LOAD_OP, IMM_MODE, R6, 0, 0x66))
    main_program.append(encode_instruction(LOAD_OP, IMM_MODE, R7, 0, 0x77))
    main_program.append(encode_instruction(DEBUG_OP, IMM_MODE, 0, 0, 0))
    
    # Generate software interrupt 0x10
    add_print_string(main_program, "Generating interrupt 0x10...")
    add_print_newline(main_program)
    main_program.append(encode_instruction(INT_OP, IMM_MODE, 0, 0, 0x10))
    
    main_program.append(encode_instruction(DEBUG_OP, IMM_MODE, 0, 0, 0))
    # This code runs after returning from interrupt
    add_print_string(main_program, "Returned from interrupt handler!")
    add_print_newline(main_program)
    
    # Print register values to verify they were preserved
    add_print_string(main_program, "Register R5 = ")
    main_program.append(encode_instruction(MOVE_OP, REG_MODE, R0_ACC, R5, 0))
    main_program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 1))  # Print int
    add_print_newline(main_program)
    
    add_print_string(main_program, "Register R6 = ")
    main_program.append(encode_instruction(MOVE_OP, REG_MODE, R0_ACC, R6, 0))
    main_program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 1))  # Print int
    add_print_newline(main_program)
    
    add_print_string(main_program, "Register R7 = ")
    main_program.append(encode_instruction(MOVE_OP, REG_MODE, R0_ACC, R7, 0))
    main_program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 1))  # Print int
    add_print_newline(main_program)
    
    # Halt the program
    add_print_string(main_program, "Program completed.")
    add_print_newline(main_program)
    main_program.append(encode_instruction(HALT_OP, IMM_MODE, 0, 0, 0))
    
    # Add main program to the beginning
    program.extend(main_program)
    
    # Calculate the offset for the interrupt handler
    interrupt_handler_offset = (INTERRUPT_HANDLER_BASE // 4) - len(program)
    
    # Pad program with NOPs until we reach the interrupt handler address
    for _ in range(interrupt_handler_offset):
        program.append(encode_instruction(NOP_OP, IMM_MODE, 0, 0, 0))
    
    # Interrupt handler code (at 0x1000)
    interrupt_handler = []
    
    add_print_string(interrupt_handler, "*** INTERRUPT HANDLER ***")
    add_print_newline(interrupt_handler)
    
    # Demonstrate that registers have been saved
    add_print_string(interrupt_handler, "Original R5 value has been preserved")
    add_print_newline(interrupt_handler)
    
    # Change register values to prove they get restored
    add_print_string(interrupt_handler, "Changing register values...")
    add_print_newline(interrupt_handler)
    interrupt_handler.append(encode_instruction(LOAD_OP, IMM_MODE, R5, 0, 0xAA))
    interrupt_handler.append(encode_instruction(LOAD_OP, IMM_MODE, R6, 0, 0xBB))
    interrupt_handler.append(encode_instruction(LOAD_OP, IMM_MODE, R7, 0, 0xCC))
    interrupt_handler.append(encode_instruction(DEBUG_OP, IMM_MODE, 0, 0, 0))
    
    # Return from interrupt
    add_print_string(interrupt_handler, "Returning from interrupt...")
    add_print_newline(interrupt_handler)
    interrupt_handler.append(encode_instruction(IRET_OP, IMM_MODE, 0, 0, 0))
    
    # Add interrupt handler to program
    program.extend(interrupt_handler)
    
    # Convert instructions to binary
    binary_program = bytearray()
    for instr in program:
        binary_program.extend(struct.pack("<I", instr))
    
    return binary_program

def main():
    """Main function to generate and output the test program."""
    binary_program = generate_interrupt_test_program()
    
    # Write to stdout in binary mode
    if hasattr(sys.stdout, 'buffer'):
        sys.stdout.buffer.write(binary_program)
    else:
        sys.stdout.write(binary_program)

if __name__ == "__main__":
    main()