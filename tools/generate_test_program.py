#!/usr/bin/env python3
"""
Test Program Generator for VM

This script generates a binary test program for the VM.
It creates a simple "Hello, World!" program that prints a message and then halts.
"""

import struct
import sys

# Instruction format:
# [Opcode: 8 bits][Mode: 4 bits][Reg1: 4 bits][Reg2: 4 bits][Immediate/Offset: 12 bits]

# Constants from instruction_set.h
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
R1_BP = 1    # Base pointer
R2_SP = 2    # Stack pointer
R3_PC = 3    # Program counter
R4_SR = 4    # Status register
R5 = 5       # General purpose register
R6 = 6       # General purpose register
R7 = 7       # General purpose register
R8 = 8       # General purpose register
R9 = 9       # General purpose register

# Memory segment base addresses
CODE_SEGMENT_BASE = 0x0000
DATA_SEGMENT_BASE = 0x4000
STACK_SEGMENT_BASE = 0x8000
HEAP_SEGMENT_BASE = 0xC000

# Selected opcodes from instruction_set.h
NOP_OP = 0x00      # No operation
LOAD_OP = 0x01     # Load value into register
STORE_OP = 0x02    # Store value to memory
MOVE_OP = 0x03     # Copy register to register
LOADB_OP = 0x04    # Load byte into register
STOREB_OP = 0x05   # Store byte to memory
ADD_OP = 0x20      # Add to register
SUB_OP = 0x21      # Subtract from register
INC_OP = 0x25      # Increment register
CMP_OP = 0x28      # Compare values
JMP_OP = 0x60      # Unconditional jump
JZ_OP = 0x61       # Jump if zero
JNZ_OP = 0x62      # Jump if not zero
SYSCALL_OP = 0x6C  # System call
LOOP_OP = 0x6F     # Decrement and jump if not zero
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

def generate_fibonacci_program():
    """Generate a program that calculates and prints Fibonacci numbers."""
    program = []
    
    # 1. Initialize R5 to 0 (first fibonacci number)
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R5, 0, 0))
    
    # 2. Initialize R6 to 1 (second fibonacci number)
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R6, 0, 1))
    
    # 3. Initialize counter R0_ACC to 10 (print 10 fibonacci numbers)
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R9, 0, 25))
    
    # Loop start (at offset 12 - 3 instructions * 4 bytes)
    loop_start = 12
    
    # 4. Move R5 to accumulator (R0_ACC) for printing
    program.append(encode_instruction(MOVE_OP, REG_MODE, R0_ACC, R5, 0))
    
    # 5. Print number using syscall 1 (print integer)
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 1))
    
    # 6. Print newline
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R0_ACC, 0, 10))  # Newline ASCII
    program.append(encode_instruction(SYSCALL_OP, IMM_MODE, 0, 0, 0))
    
    # 7. Calculate next Fibonacci number (R5 + R6) into R0_ACC
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R0_ACC, 0, 0))
    program.append(encode_instruction(ADD_OP, REG_MODE, R0_ACC, R5, 0))
    program.append(encode_instruction(ADD_OP, REG_MODE, R0_ACC, R6, 0))
    
    # 8. Shift: R5 = R6, R6 = R0_ACC
    program.append(encode_instruction(MOVE_OP, REG_MODE, R5, R6, 0))
    program.append(encode_instruction(MOVE_OP, REG_MODE, R6, R0_ACC, 0))
    
    # 9. Decrement counter
    program.append(encode_instruction(LOAD_OP, IMM_MODE, R8, 0, 1))
    program.append(encode_instruction(SUB_OP, REG_MODE, R9, R8, 0))
    
    # 10. Loop if counter > 0
    end_of_loop = len(program) * 4  # Each instruction is 4 bytes
    program.append(encode_instruction(JNZ_OP, IMM_MODE, 0, 0, loop_start & 0xFFF))
    
    # 11. HALT instruction at the end
    program.append(encode_instruction(HALT_OP, IMM_MODE, 0, 0, 0))
    
    # Convert instructions to binary
    binary_program = bytearray()
    for instr in program:
        binary_program.extend(struct.pack("<I", instr))
    
    return binary_program

def main():
    """Main function to generate and output the test program."""
    binary_program = generate_fibonacci_program()
    
    # Write to stdout in binary mode
    if hasattr(sys.stdout, 'buffer'):
        sys.stdout.buffer.write(binary_program)
    else:
        sys.stdout.write(binary_program)

if __name__ == "__main__":
    main()