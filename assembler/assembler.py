#!/usr/bin/env python3

import re
import struct
import sys
import os
from enum import IntEnum

# Addressing modes
class AddressingMode(IntEnum):
    IMM = 0x0   # Immediate: Value in instruction
    REG = 0x1   # Register: Value in register
    MEM = 0x2   # Memory: Direct address
    REGM = 0x3  # Register Indirect: [Reg]
    IDX = 0x4   # Indexed: [Reg + Offset]
    STK = 0x5   # Stack Relative: [SP + Offset]
    BAS = 0x6   # Base Relative: [BP + Offset]

# Register definitions
class Register(IntEnum):
    R0_ACC = 0  # Accumulator
    R1_BP = 1   # Base pointer
    R2_SP = 2   # Stack pointer
    R3_PC = 3   # Program counter
    R4_SR = 4   # Status register
    R5 = 5
    R6 = 6
    R7 = 7
    R8 = 8
    R9 = 9
    R10 = 10
    R11 = 11
    R12 = 12
    R13 = 13
    R14 = 14
    R15_LR = 15  # Link register

# Memory segment base addresses
CODE_SEGMENT_BASE = 0x0000
DATA_SEGMENT_BASE = 0x4000
STACK_SEGMENT_BASE = 0x8000
HEAP_SEGMENT_BASE = 0xC000

# Define opcodes from instruction_set.h
class Opcode(IntEnum):
    # Data Transfer Instructions (0x00-0x1F)
    NOP = 0x00
    LOAD = 0x01
    STORE = 0x02
    MOVE = 0x03
    LOADB = 0x04
    STOREB = 0x05
    LOADW = 0x06
    STOREW = 0x07
    LEA = 0x08
    
    # Arithmetic Instructions (0x20-0x3F)
    ADD = 0x20
    SUB = 0x21
    MUL = 0x22
    DIV = 0x23
    MOD = 0x24
    INC = 0x25
    DEC = 0x26
    NEG = 0x27
    CMP = 0x28
    ADDC = 0x2A
    SUBC = 0x2B
    
    # Logical Instructions (0x40-0x5F)
    AND = 0x40
    OR = 0x41
    XOR = 0x42
    NOT = 0x43
    SHL = 0x44
    SHR = 0x45
    SAR = 0x46
    ROL = 0x47
    ROR = 0x48
    TEST = 0x49
    
    # Control Flow Instructions (0x60-0x7F)
    JMP = 0x60
    JZ = 0x61
    JNZ = 0x62
    JN = 0x63
    JP = 0x64
    JO = 0x65
    JC = 0x66
    JBE = 0x67
    JA = 0x68
    CALL = 0x6A
    RET = 0x6B
    SYSCALL = 0x6C
    LOOP = 0x6F
    
    # Stack Instructions (0x80-0x9F)
    PUSH = 0x80
    POP = 0x81
    PUSHF = 0x82
    POPF = 0x83
    PUSHA = 0x84
    POPA = 0x85
    ENTER = 0x86
    LEAVE = 0x87
    
    # System Instructions (0xA0-0xBF)
    HALT = 0xA0
    INT = 0xA1
    CLI = 0xA2
    STI = 0xA3
    IRET = 0xA4
    IN = 0xA5
    OUT = 0xA6
    CPUID = 0xA7
    RESET = 0xA8
    DEBUG = 0xA9
    
    # Memory Control Instructions (0xC0-0xDF)
    ALLOC = 0xC0
    FREE = 0xC1
    MEMCPY = 0xC2
    MEMSET = 0xC3
    PROTECT = 0xC4

# Instruction format validator
class InstructionFormat:
    def __init__(self):
        # Define instruction formats: (num_operands, [allowed_addr_modes], desc)
        self.formats = {
            # No operand instructions
            "NOP": (0, [], "No operation"),
            "PUSHF": (0, [], "Push flags onto stack"),
            "POPF": (0, [], "Pop flags from stack"),
            "PUSHA": (0, [], "Push all registers onto stack"),
            "POPA": (0, [], "Pop all registers from stack"),
            "LEAVE": (0, [], "Destroy stack frame"),
            "HALT": (0, [], "Halt execution"),
            "CLI": (0, [], "Clear interrupt flag"),
            "STI": (0, [], "Set interrupt flag"),
            "IRET": (0, [], "Return from interrupt"),
            "CPUID": (0, [], "Get CPU information"),
            "RESET": (0, [], "Reset VM"),
            "DEBUG": (0, [], "Trigger debugger"),
            
            # Single register operand
            "INC": (1, [AddressingMode.REG], "Increment register"),
            "DEC": (1, [AddressingMode.REG], "Decrement register"),
            "NEG": (1, [AddressingMode.REG], "Negate register"),
            "NOT": (1, [AddressingMode.REG], "Bitwise NOT"),
            "POP": (1, [AddressingMode.REG], "Pop from stack to register"),
            
            # RET - optional immediate
            "RET": (-1, [AddressingMode.IMM], "Return from subroutine"),
            
            # PUSH - register or immediate
            "PUSH": (1, [AddressingMode.REG, AddressingMode.IMM], "Push onto stack"),
            
            # Jump/Call - various addressing modes
            "JMP": (1, [AddressingMode.IMM, AddressingMode.REG, AddressingMode.REGM, AddressingMode.IDX], "Jump unconditionally"),
            "JZ": (1, [AddressingMode.IMM, AddressingMode.REG, AddressingMode.REGM, AddressingMode.IDX], "Jump if zero"),
            "JNZ": (1, [AddressingMode.IMM, AddressingMode.REG, AddressingMode.REGM, AddressingMode.IDX], "Jump if not zero"),
            "JN": (1, [AddressingMode.IMM, AddressingMode.REG, AddressingMode.REGM, AddressingMode.IDX], "Jump if negative"),
            "JP": (1, [AddressingMode.IMM, AddressingMode.REG, AddressingMode.REGM, AddressingMode.IDX], "Jump if positive"),
            "JO": (1, [AddressingMode.IMM, AddressingMode.REG, AddressingMode.REGM, AddressingMode.IDX], "Jump if overflow"),
            "JC": (1, [AddressingMode.IMM, AddressingMode.REG, AddressingMode.REGM, AddressingMode.IDX], "Jump if carry"),
            "JBE": (1, [AddressingMode.IMM, AddressingMode.REG, AddressingMode.REGM, AddressingMode.IDX], "Jump if below or equal"),
            "JA": (1, [AddressingMode.IMM, AddressingMode.REG, AddressingMode.REGM, AddressingMode.IDX], "Jump if above"),
            "CALL": (1, [AddressingMode.IMM, AddressingMode.REG, AddressingMode.REGM, AddressingMode.IDX], "Call subroutine"),
            
            # Single immediate operand
            "ENTER": (1, [AddressingMode.IMM], "Create stack frame"),
            "INT": (1, [AddressingMode.IMM], "Generate interrupt"),
            "SYSCALL": (1, [AddressingMode.IMM], "System call"),
            
            # Register + Immediate operand
            "IN": (2, [(AddressingMode.REG, AddressingMode.IMM)], "Input from port"),
            
            # Port + value
            "OUT": (2, [(AddressingMode.IMM, [AddressingMode.REG, AddressingMode.IMM])], "Output to port"),
            
            # Register + target address
            "LOOP": (2, [(AddressingMode.REG, AddressingMode.IMM)], "Decrement and jump if not zero"),
            
            # Move (register to register)
            "MOVE": (2, [(AddressingMode.REG, AddressingMode.REG)], "Move register to register"),
            
            # Two-operand instructions with register destination
            "LOAD": (2, [(AddressingMode.REG, [AddressingMode.IMM, AddressingMode.MEM, AddressingMode.REGM, AddressingMode.IDX, AddressingMode.STK, AddressingMode.BAS])], "Load value into register"),
            "LOADB": (2, [(AddressingMode.REG, [AddressingMode.IMM, AddressingMode.MEM, AddressingMode.REGM, AddressingMode.IDX, AddressingMode.STK, AddressingMode.BAS])], "Load byte into register"),
            "LOADW": (2, [(AddressingMode.REG, [AddressingMode.IMM, AddressingMode.MEM, AddressingMode.REGM, AddressingMode.IDX, AddressingMode.STK, AddressingMode.BAS])], "Load word into register"),
            "LEA": (2, [(AddressingMode.REG, [AddressingMode.MEM, AddressingMode.REGM, AddressingMode.IDX, AddressingMode.STK, AddressingMode.BAS])], "Load effective address"),
            
            # Arithmetic/Logical operations
            "ADD": (2, [(AddressingMode.REG, [AddressingMode.REG, AddressingMode.IMM, AddressingMode.MEM, AddressingMode.REGM, AddressingMode.IDX, AddressingMode.STK, AddressingMode.BAS])], "Add to register"),
            "SUB": (2, [(AddressingMode.REG, [AddressingMode.REG, AddressingMode.IMM, AddressingMode.MEM, AddressingMode.REGM, AddressingMode.IDX, AddressingMode.STK, AddressingMode.BAS])], "Subtract from register"),
            "MUL": (2, [(AddressingMode.REG, [AddressingMode.REG, AddressingMode.IMM, AddressingMode.MEM, AddressingMode.REGM, AddressingMode.IDX, AddressingMode.STK, AddressingMode.BAS])], "Multiply register"),
            "DIV": (2, [(AddressingMode.REG, [AddressingMode.REG, AddressingMode.IMM, AddressingMode.MEM, AddressingMode.REGM, AddressingMode.IDX, AddressingMode.STK, AddressingMode.BAS])], "Divide register"),
            "MOD": (2, [(AddressingMode.REG, [AddressingMode.REG, AddressingMode.IMM, AddressingMode.MEM, AddressingMode.REGM, AddressingMode.IDX, AddressingMode.STK, AddressingMode.BAS])], "Modulo operation"),
            "AND": (2, [(AddressingMode.REG, [AddressingMode.REG, AddressingMode.IMM, AddressingMode.MEM, AddressingMode.REGM, AddressingMode.IDX, AddressingMode.STK, AddressingMode.BAS])], "Bitwise AND"),
            "OR": (2, [(AddressingMode.REG, [AddressingMode.REG, AddressingMode.IMM, AddressingMode.MEM, AddressingMode.REGM, AddressingMode.IDX, AddressingMode.STK, AddressingMode.BAS])], "Bitwise OR"),
            "XOR": (2, [(AddressingMode.REG, [AddressingMode.REG, AddressingMode.IMM, AddressingMode.MEM, AddressingMode.REGM, AddressingMode.IDX, AddressingMode.STK, AddressingMode.BAS])], "Bitwise XOR"),
            "SHL": (2, [(AddressingMode.REG, [AddressingMode.REG, AddressingMode.IMM])], "Shift left"),
            "SHR": (2, [(AddressingMode.REG, [AddressingMode.REG, AddressingMode.IMM])], "Shift right (logical)"),
            "SAR": (2, [(AddressingMode.REG, [AddressingMode.REG, AddressingMode.IMM])], "Shift right (arithmetic)"),
            "ROL": (2, [(AddressingMode.REG, [AddressingMode.REG, AddressingMode.IMM])], "Rotate left"),
            "ROR": (2, [(AddressingMode.REG, [AddressingMode.REG, AddressingMode.IMM])], "Rotate right"),
            "TEST": (2, [(AddressingMode.REG, [AddressingMode.REG, AddressingMode.IMM, AddressingMode.MEM, AddressingMode.REGM, AddressingMode.IDX, AddressingMode.STK, AddressingMode.BAS])], "Test bits"),
            "CMP": (2, [(AddressingMode.REG, [AddressingMode.REG, AddressingMode.IMM, AddressingMode.MEM, AddressingMode.REGM, AddressingMode.IDX, AddressingMode.STK, AddressingMode.BAS])], "Compare"),
            "ADDC": (2, [(AddressingMode.REG, [AddressingMode.REG, AddressingMode.IMM, AddressingMode.MEM, AddressingMode.REGM, AddressingMode.IDX, AddressingMode.STK, AddressingMode.BAS])], "Add with carry"),
            "SUBC": (2, [(AddressingMode.REG, [AddressingMode.REG, AddressingMode.IMM, AddressingMode.MEM, AddressingMode.REGM, AddressingMode.IDX, AddressingMode.STK, AddressingMode.BAS])], "Subtract with carry"),
            
            # Store operations
            "STORE": (2, [(AddressingMode.REG, [AddressingMode.MEM, AddressingMode.REGM, AddressingMode.IDX, AddressingMode.STK, AddressingMode.BAS])], "Store to memory"),
            "STOREB": (2, [(AddressingMode.REG, [AddressingMode.MEM, AddressingMode.REGM, AddressingMode.IDX, AddressingMode.STK, AddressingMode.BAS])], "Store byte to memory"),
            "STOREW": (2, [(AddressingMode.REG, [AddressingMode.MEM, AddressingMode.REGM, AddressingMode.IDX, AddressingMode.STK, AddressingMode.BAS])], "Store word to memory"),
            
            # Memory management
            "ALLOC": (2, [(AddressingMode.REG, [AddressingMode.REG, AddressingMode.IMM])], "Allocate heap memory"),
            "FREE": (1, [AddressingMode.REG], "Free heap memory"),
            "MEMCPY": (3, [(AddressingMode.REG, AddressingMode.REG, AddressingMode.IMM)], "Copy memory block"),
            "MEMSET": (3, [(AddressingMode.REG, AddressingMode.REG, AddressingMode.IMM)], "Set memory block"),
            "PROTECT": (2, [(AddressingMode.REG, [AddressingMode.REG, AddressingMode.IMM])], "Set memory protection"),
        }
    
    def validate(self, opcode, operands, addr_modes):
        """
        Validate that the instruction format is correct.
        Returns (valid, error_message)
        """
        if opcode not in self.formats:
            return False, f"Unknown opcode: {opcode}"
        
        fmt = self.formats[opcode]
        num_operands = fmt[0]
        
        # Special case for RET which has optional operand
        if num_operands == -1:
            if len(operands) > 1:
                return False, f"{opcode} expects at most 1 operand, got {len(operands)}"
            return True, ""
        
        # Check number of operands
        if len(operands) != num_operands:
            return False, f"{opcode} expects {num_operands} operand(s), got {len(operands)}"
        
        # If no operands, we're done
        if num_operands == 0:
            return True, ""
        
        # Check addressing modes if there are mode constraints
        allowed_modes = fmt[1]
        if allowed_modes:
            # Single operand with list of allowed modes
            if num_operands == 1 and isinstance(allowed_modes[0], int):
                if addr_modes[0] not in allowed_modes:
                    modes_str = ", ".join([f"{mode.name}" for mode in allowed_modes])
                    return False, f"{opcode} expects operand with addressing mode(s): {modes_str}, got {addr_modes[0].name}"
            
            # Two operands with tuple of (dest, src) mode constraints
            elif num_operands == 2 and len(allowed_modes) == 1 and isinstance(allowed_modes[0], tuple):
                dest_mode, src_modes = allowed_modes[0]
                
                # If dest_mode is a list, check if the actual mode is in the list
                if isinstance(dest_mode, list):
                    if addr_modes[0] not in dest_mode:
                        modes_str = ", ".join([f"{mode.name}" for mode in dest_mode])
                        return False, f"{opcode} expects first operand with addressing mode(s): {modes_str}, got {addr_modes[0].name}"
                # Otherwise, just check equality
                elif addr_modes[0] != dest_mode:
                    return False, f"{opcode} expects first operand with addressing mode: {dest_mode.name}, got {addr_modes[0].name}"
                
                # If src_modes is a list, check if the actual mode is in the list
                if isinstance(src_modes, list):
                    if addr_modes[1] not in src_modes:
                        modes_str = ", ".join([f"{mode.name}" for mode in src_modes])
                        return False, f"{opcode} expects second operand with addressing mode(s): {modes_str}, got {addr_modes[1].name}"
                # Otherwise, just check equality
                elif addr_modes[1] != src_modes:
                    return False, f"{opcode} expects second operand with addressing mode: {src_modes.name}, got {addr_modes[1].name}"
        
        return True, ""

class Assembler:
    def __init__(self):
        self.labels = {}
        self.instructions = []
        self.data = []
        self.current_section = ".text"
        self.address = CODE_SEGMENT_BASE
        self.data_address = DATA_SEGMENT_BASE
        self.unresolved_references = []
        self.errors = []
        self.instruction_format = InstructionFormat()
        
        # Register names mapping
        self.registers = {
            "R0": 0, "ACC": 0, "R0_ACC": 0,
            "R1": 1, "BP": 1, "R1_BP": 1,
            "R2": 2, "SP": 2, "R2_SP": 2,
            "R3": 3, "PC": 3, "R3_PC": 3,
            "R4": 4, "SR": 4, "R4_SR": 4,
            "R5": 5, "R6": 6, "R7": 7, "R8": 8, "R9": 9,
            "R10": 10, "R11": 11, "R12": 12, "R13": 13, "R14": 14,
            "R15": 15, "LR": 15, "R15_LR": 15
        }
        
        # Opcode mapping
        self.opcodes = {op.name: op.value for op in Opcode}
        
        # Current line info for error messages
        self.current_file = ""
        self.current_line = 0
        
        # Include file tracking to prevent circular includes
        self.included_files = set()
    
    def error(self, message):
        """Report an error with current file and line info."""
        error_msg = f"{self.current_file}:{self.current_line}: {message}"
        self.errors.append(error_msg)
    
    def encode_instruction(self, opcode, mode, reg1, reg2, immediate):
        """Encode a VM instruction into a 32-bit value."""
        # Ensure fields are within their bit limits
        opcode = opcode & 0xFF        # 8 bits
        mode = mode & 0x0F            # 4 bits
        reg1 = reg1 & 0x0F            # 4 bits
        
        # If in immediate mode, split 16-bit immediate across reg2 and immediate fields
        if mode in [AddressingMode.IMM, AddressingMode.MEM, AddressingMode.STK, AddressingMode.BAS]:
            reg2 = (immediate >> 12) & 0xF  # High 4 bits
            immediate = immediate & 0xFFF   # Low 12 bits
        else:
            reg2 = reg2 & 0x0F            # 4 bits
            immediate = immediate & 0xFFF  # 12 bits
        
        # Build the instruction
        instruction = (opcode << 24) | (mode << 20) | (reg1 << 16) | (reg2 << 12) | immediate
        return instruction
    
    def parse_register(self, token):
        """Parse a register name to its numeric value."""
        token = token.upper()
        if token in self.registers:
            return self.registers[token]
        
        # Try to parse Rx format
        if token.startswith('R') and token[1:].isdigit():
            reg_num = int(token[1:])
            if 0 <= reg_num <= 15:
                return reg_num
        
        self.error(f"Invalid register: {token}")
        return 0
    
    def parse_immediate(self, token, allow_unresolved=False):
        """Parse an immediate value or label."""
        # Check if it's a label reference
        if re.match(r'^[A-Za-z_.][A-Za-z0-9_.]*$', token):
            if token in self.labels:
                return self.labels[token]
            elif allow_unresolved:
                # We'll resolve this later
                return 0  # Placeholder value
            else:
                self.error(f"Undefined symbol: {token}")
                return 0
        
        # Try to parse numeric value
        try:
            # Check for different number formats
            if token.startswith('0x') or token.startswith('0X'):
                return int(token, 16)
            elif token.startswith('0b') or token.startswith('0B'):
                return int(token, 2)
            elif token.startswith('0') and len(token) > 1 and token[1] not in 'xXbB':
                return int(token, 8)
            else:
                return int(token)
        except ValueError:
            self.error(f"Invalid numeric value: {token}")
            return 0
    
    def parse_operand(self, operand_str, is_dest=False):
        """
        Parse an operand string into addressing mode and values.
        Returns (addressing_mode, reg1, reg2, immediate)
        """
        operand_str = operand_str.strip()
        
        # Immediate value: #123, #0xFF
        if operand_str.startswith('#'):
            value = self.parse_immediate(operand_str[1:], allow_unresolved=True)
            return AddressingMode.IMM, 0, 0, value
        
        # Memory bracket notation: [expr]
        bracket_match = re.match(r'\[(.*)\]', operand_str)
        if bracket_match:
            expr = bracket_match.group(1).strip()
            
            # Check for various forms of memory addressing
            # Register indirect: [R0]
            if expr in self.registers or (expr.startswith('R') and expr[1:].isdigit()):
                reg = self.parse_register(expr)
                
                # Special case for SP and BP based addressing
                if reg == Register.R2_SP:
                    return AddressingMode.STK, 0, 0, 0
                elif reg == Register.R1_BP:
                    return AddressingMode.BAS, 0, 0, 0
                else:
                    return AddressingMode.REGM, reg, 0, 0
            
            # Register + offset: [R0+123], [SP+8], etc.
            plus_match = re.match(r'(.*?)\s*\+\s*(.*)', expr)
            if plus_match:
                base, offset = plus_match.group(1).strip(), plus_match.group(2).strip()
                offset_sign = 1  # Positive
                
                # Base is a register
                if base in self.registers or (base.startswith('R') and base[1:].isdigit()):
                    reg = self.parse_register(base)
                    imm = self.parse_immediate(offset, allow_unresolved=True)
                    
                    # Special case for SP and BP based addressing
                    if reg == Register.R2_SP:
                        return AddressingMode.STK, 0, 0, imm
                    elif reg == Register.R1_BP:
                        return AddressingMode.BAS, 0, 0, imm
                    else:
                        return AddressingMode.IDX, reg, 0, imm
                # Base is a symbol
                else:
                    # Use base as a symbol and parse offset
                    if offset.isdigit() or (offset.startswith('0x') and all(c in '0123456789abcdefABCDEF' for c in offset[2:])):
                        # Simple numeric offset
                        imm_offset = self.parse_immediate(offset, allow_unresolved=False)
                        
                        # Get the base symbol's value
                        if base in self.labels:
                            base_value = self.labels[base]
                        else:
                            # Add to unresolved references
                            self.unresolved_references.append((len(self.instructions), base, 'imm'))
                            base_value = 0  # Placeholder
                        
                        # Combine base + offset
                        value = base_value + imm_offset
                        return AddressingMode.MEM, 0, 0, value
                    else:
                        # More complex expression - not fully supported
                        self.error(f"Complex expressions not supported: {expr}")
                        return AddressingMode.MEM, 0, 0, 0
                        
            # Register - offset: [R0-123], [SP-8], etc.
            minus_match = re.match(r'(.*?)\s*\-\s*(.*)', expr)
            if minus_match:
                base, offset = minus_match.group(1).strip(), minus_match.group(2).strip()
                offset_sign = -1  # Negative
                
                # Base is a register
                if base in self.registers or (base.startswith('R') and base[1:].isdigit()):
                    reg = self.parse_register(base)
                    imm = self.parse_immediate(offset, allow_unresolved=True)
                    
                    # For negative offsets, we need to use 2's complement for 12-bit value
                    # since the immediate field is treated as unsigned in instruction encoding
                    neg_imm = (-imm) & 0xFFF  # Apply 2's complement in 12-bit space
                    
                    # Special case for SP and BP based addressing
                    if reg == Register.R2_SP:
                        return AddressingMode.STK, 0, 0, neg_imm
                    elif reg == Register.R1_BP:
                        return AddressingMode.BAS, 0, 0, neg_imm
                    else:
                        return AddressingMode.IDX, reg, 0, neg_imm
                # Base is a symbol
                else:
                    # Use base as a symbol and parse offset
                    if offset.isdigit() or (offset.startswith('0x') and all(c in '0123456789abcdefABCDEF' for c in offset[2:])):
                        # Simple numeric offset
                        imm_offset = self.parse_immediate(offset, allow_unresolved=False)
                        
                        # Get the base symbol's value
                        if base in self.labels:
                            base_value = self.labels[base]
                        else:
                            # Add to unresolved references
                            self.unresolved_references.append((len(self.instructions), base, 'imm'))
                            base_value = 0  # Placeholder
                        
                        # Combine base - offset
                        value = base_value - imm_offset
                        return AddressingMode.MEM, 0, 0, value
                    else:
                        # More complex expression - not fully supported
                        self.error(f"Complex expressions not supported: {expr}")
                        return AddressingMode.MEM, 0, 0, 0
            
            # Direct memory address: [1234], [0xFF], [LABEL]
            value = self.parse_immediate(expr, allow_unresolved=True)
            return AddressingMode.MEM, 0, 0, value
        
        # Must be a register if not immediate or memory
        if operand_str in self.registers or (operand_str.startswith('R') and operand_str[1:].isdigit()):
            return AddressingMode.REG, self.parse_register(operand_str), 0, 0
        
        # Otherwise assume it's a label/immediate value without # prefix
        value = self.parse_immediate(operand_str, allow_unresolved=True)
        return AddressingMode.IMM, 0, 0, value
    
    def process_directive(self, directive, args):
        """Process an assembly directive."""
        directive = directive.lower()
        
        if directive == ".text":
            self.current_section = ".text"
            # Align to 4 bytes for instructions
            self.address = (self.address + 3) & ~3
        
        elif directive == ".data":
            self.current_section = ".data"
            # Move to data segment if we're still in code segment
            if self.data_address == DATA_SEGMENT_BASE:
                self.data_address = DATA_SEGMENT_BASE
        
        elif directive == ".byte":
            if not args:
                self.error(".byte directive requires at least one value")
                return
            
            if self.current_section != ".data":
                self.error(".byte directive can only appear in .data section")
                return
            
            # Parse comma-separated values
            for value_str in args.split(','):
                value_str = value_str.strip()
                if not value_str:
                    continue
                
                value = self.parse_immediate(value_str, allow_unresolved=True)
                self.data.append(value & 0xFF)  # Ensure byte size
                self.data_address += 1
        
        elif directive == ".word":
            if not args:
                self.error(".word directive requires at least one value")
                return
            
            if self.current_section != ".data":
                self.error(".word directive can only appear in .data section")
                return
            
            # Align to 2 bytes
            if self.data_address % 2 != 0:
                self.data.append(0)
                self.data_address += 1
            
            # Parse comma-separated values
            for value_str in args.split(','):
                value_str = value_str.strip()
                if not value_str:
                    continue
                
                value = self.parse_immediate(value_str, allow_unresolved=True)
                self.data.append(value & 0xFF)  # Low byte
                self.data.append((value >> 8) & 0xFF)  # High byte
                self.data_address += 2
        
        elif directive == ".dword":
            if not args:
                self.error(".dword directive requires at least one value")
                return
            
            if self.current_section != ".data":
                self.error(".dword directive can only appear in .data section")
                return
            
            # Align to 4 bytes
            while self.data_address % 4 != 0:
                self.data.append(0)
                self.data_address += 1
            
            # Parse comma-separated values
            for value_str in args.split(','):
                value_str = value_str.strip()
                if not value_str:
                    continue
                
                value = self.parse_immediate(value_str, allow_unresolved=True)
                self.data.append(value & 0xFF)  # Byte 0
                self.data.append((value >> 8) & 0xFF)  # Byte 1
                self.data.append((value >> 16) & 0xFF)  # Byte 2
                self.data.append((value >> 24) & 0xFF)  # Byte 3
                self.data_address += 4
        
        elif directive == ".ascii":
            if not args:
                self.error(".ascii directive requires a string")
                return
            
            if self.current_section != ".data":
                self.error(".ascii directive can only appear in .data section")
                return
            
            # Extract the string (handle quotes)
            match = re.match(r'"([^"]*)"', args)
            if not match:
                self.error(f"Invalid string format for .ascii: {args}")
                return
            
            string = match.group(1)
            
            # Process escape sequences
            i = 0
            actual_bytes = 0  # Track actual bytes added to memory
            while i < len(string):
                if string[i] == '\\' and i + 1 < len(string):
                    if string[i + 1] == 'n':
                        self.data.append(ord('\n'))
                    elif string[i + 1] == 't':
                        self.data.append(ord('\t'))
                    elif string[i + 1] == 'r':
                        self.data.append(ord('\r'))
                    elif string[i + 1] == '0':
                        self.data.append(0)
                    elif string[i + 1] == '\\':
                        self.data.append(ord('\\'))
                    elif string[i + 1] == '"':
                        self.data.append(ord('"'))
                    else:
                        self.data.append(ord(string[i + 1]))
                    i += 2  # Skip both backslash and the escape character
                    actual_bytes += 1  # But only one byte is added to memory
                else:
                    self.data.append(ord(string[i]))
                    i += 1
                    actual_bytes += 1
            
            self.data_address += actual_bytes
        
        elif directive == ".asciiz":
            if not args:
                self.error(".asciiz directive requires a string")
                return
            
            if self.current_section != ".data":
                self.error(".asciiz directive can only appear in .data section")
                return
            
            # Extract the string (handle quotes)
            match = re.match(r'"([^"]*)"', args)
            if not match:
                self.error(f"Invalid string format for .asciiz: {args}")
                return
            
            string = match.group(1)
            
            # Process escape sequences (same as .ascii)
            i = 0
            actual_bytes = 0  # Track actual bytes added to memory
            while i < len(string):
                if string[i] == '\\' and i + 1 < len(string):
                    if string[i + 1] == 'n':
                        self.data.append(ord('\n'))
                    elif string[i + 1] == 't':
                        self.data.append(ord('\t'))
                    elif string[i + 1] == 'r':
                        self.data.append(ord('\r'))
                    elif string[i + 1] == '0':
                        self.data.append(0)
                    elif string[i + 1] == '\\':
                        self.data.append(ord('\\'))
                    elif string[i + 1] == '"':
                        self.data.append(ord('"'))
                    else:
                        self.data.append(ord(string[i + 1]))
                    i += 2  # Skip both backslash and the escape character
                    actual_bytes += 1  # But only one byte is added to memory
                else:
                    self.data.append(ord(string[i]))
                    i += 1
                    actual_bytes += 1
            
            # Add null terminator
            self.data.append(0)
            self.data_address += actual_bytes + 1
        
        elif directive == ".space" or directive == ".skip":
            if not args:
                self.error(f"{directive} directive requires a size")
                return
            
            # Parse size
            try:
                size = self.parse_immediate(args.split()[0], allow_unresolved=False)
            except:
                self.error(f"Invalid size for {directive}: {args}")
                return
            
            if size <= 0:
                self.error(f"Size for {directive} must be positive: {size}")
                return
            
            # Fill with zeros
            if self.current_section == ".text":
                # Fill code section with NOP instructions (must be word-aligned)
                size = (size + 3) & ~3  # Round up to multiple of 4
                for _ in range(0, size, 4):
                    self.instructions.append(0)  # NOP instruction
                self.address += size
            else:
                # Fill data section with zeros
                for _ in range(size):
                    self.data.append(0)
                self.data_address += size
        
        elif directive == ".align":
            if not args:
                self.error(".align directive requires an alignment value")
                return
            
            # Parse alignment
            try:
                alignment = self.parse_immediate(args.split()[0], allow_unresolved=False)
            except:
                self.error(f"Invalid alignment: {args}")
                return
            
            if alignment <= 0 or (alignment & (alignment - 1)) != 0:
                self.error(f"Alignment must be a positive power of 2: {args}")
                return
            
            # Calculate padding needed
            if self.current_section == ".text":
                current = self.address
                aligned = (current + alignment - 1) & ~(alignment - 1)
                padding = aligned - current
                
                # Add NOP instructions for padding
                for _ in range(0, padding, 4):
                    self.instructions.append(0)  # NOP instruction
                self.address += padding
            else:
                current = self.data_address
                aligned = (current + alignment - 1) & ~(alignment - 1)
                padding = aligned - current
                
                # Add zero bytes for padding
                for _ in range(padding):
                    self.data.append(0)
                self.data_address += padding
        
        elif directive == ".equ" or directive == ".set":
            parts = args.split(',', 1)
            if len(parts) != 2:
                self.error(f"Invalid format for {directive}: {args}")
                return
            
            name, value_str = parts
            name = name.strip()
            value_str = value_str.strip()
            if not re.match(r'^[A-Za-z_.][A-Za-z0-9_.]*$', name):
                self.error(f"Invalid symbol name: {name}")
                return
            
            value = self.parse_immediate(value_str, allow_unresolved=False)
            self.labels[name] = value
        
        elif directive == ".org":
            if not args:
                self.error(".org directive requires an address")
                return
            
            # Parse address
            try:
                address = self.parse_immediate(args.split()[0], allow_unresolved=False)
            except:
                self.error(f"Invalid address for .org: {args}")
                return
            
            if self.current_section == ".text":
                if address < self.address:
                    self.error(f"Cannot move address backward: {args}")
                    return
                # Pad with NOPs to reach new address
                while self.address < address:
                    self.instructions.append(0)  # NOP instruction
                    self.address += 4
            else:
                if address < self.data_address:
                    self.error(f"Cannot move data address backward: {args}")
                    return
                # Pad with zeros to reach new address
                while self.data_address < address:
                    self.data.append(0)
                    self.data_address += 1
        
        elif directive == ".include":
            if not args:
                self.error(".include directive requires a filename")
                return
            
            # Extract the filename
            match = re.match(r'"([^"]*)"', args)
            if not match:
                self.error(f"Invalid filename format for .include: {args}")
                return
            
            filename = match.group(1)
            self.include_file(filename)
        
        else:
            self.error(f"Unknown directive: {directive}")
    
    def process_instruction(self, opcode_str, operands_str):
        """Process an assembly instruction."""
        if not opcode_str:
            return
        
        opcode_str = opcode_str.upper()
        if opcode_str not in self.opcodes:
            self.error(f"Unknown opcode: {opcode_str}")
            return
        
        opcode = self.opcodes[opcode_str]
        
        # Parse operands
        operands = []
        operand_modes = []
        for_source = {}  # Track source label references
        
        if operands_str:
            raw_operands = operands_str.split(',')
            for i, raw_op in enumerate(raw_operands):
                raw_op = raw_op.strip()
                if not raw_op:
                    continue
                
                # Parse the operand
                mode, reg1, reg2, imm = self.parse_operand(raw_op, is_dest=(i==0))
                operands.append((mode, reg1, reg2, imm))
                operand_modes.append(mode)
                
                # Check if this might be a label reference in immediate mode
                if mode == AddressingMode.IMM and re.match(r'^[A-Za-z_.][A-Za-z0-9_.]*$', raw_op.lstrip('#')):
                    symbol = raw_op.lstrip('#')
                    if symbol not in self.labels:
                        for_source[i] = symbol
                
                # Check if this might be a label reference in memory mode
                elif mode == AddressingMode.MEM:
                    # Extract the expression inside brackets
                    bracket_match = re.match(r'\[(.*)\]', raw_op)
                    if bracket_match:
                        expr = bracket_match.group(1).strip()
                        # Check if it's a label
                        if re.match(r'^[A-Za-z_.][A-Za-z0-9_.]*$', expr) and expr not in self.labels:
                            for_source[i] = expr
        
        # Validate instruction format
        valid, error = self.instruction_format.validate(opcode_str, operands, operand_modes)
        if not valid:
            self.error(error)
            return
        
        # Encode instruction based on opcode and operands
        if not operands:
            # No operands (e.g., NOP, HALT)
            instruction = self.encode_instruction(opcode, AddressingMode.IMM, 0, 0, 0)
        
        elif len(operands) == 1:
            # Single operand instructions
            mode, reg1, reg2, imm = operands[0]
            instruction = self.encode_instruction(opcode, mode, reg1, reg2, imm)
            
            # Track unresolved references
            if 0 in for_source:
                self.unresolved_references.append((len(self.instructions), for_source[0], 'imm'))
        
        elif len(operands) == 2:
            # Two operand instructions
            mode1, reg1, _, _ = operands[0]
            mode2, reg2_val, _, imm = operands[1]
            
            # For most two-operand instructions
            if opcode_str in ["MOVE"]:
                # Register to register
                instruction = self.encode_instruction(opcode, AddressingMode.REG, reg1, reg2_val, 0)
            
            elif mode1 == AddressingMode.REG:
                # Destination is a register
                instruction = self.encode_instruction(opcode, mode2, reg1, reg2_val, imm)
                
                # Track unresolved references
                if 1 in for_source:
                    self.unresolved_references.append((len(self.instructions), for_source[1], 'imm'))
            
            else:
                # Special case, like OUT port, value
                self.error(f"Unsupported addressing mode for {opcode_str}")
                return
        
        elif len(operands) == 3:
            # Three operand instructions (like MEMCPY)
            # These need special handling based on the opcode
            self.error(f"Three-operand instruction {opcode_str} not fully implemented")
            return
        
        else:
            self.error(f"Too many operands for {opcode_str}")
            return
        
        # Add the encoded instruction
        self.instructions.append(instruction)
        self.address += 4
    
    def process_line(self, line):
        """Process a single line of assembly code."""
        # Remove comments
        line = re.sub(r';.*$', '', line).strip()
        if not line:
            return
        
        # Check for label
        label_match = re.match(r'^([A-Za-z_.][A-Za-z0-9_.]*):(.*)$', line)
        if label_match:
            label, remainder = label_match.groups()
            
            # Store label address
            if self.current_section == ".text":
                self.labels[label] = self.address
            else:
                # For data segment, ensure labels are aligned to 4-byte boundaries
                if self.data_address % 4 != 0:
                    # Add padding bytes
                    pad_needed = 4 - (self.data_address % 4)
                    for _ in range(pad_needed):
                        self.data.append(0)
                    self.data_address += pad_needed
                self.labels[label] = self.data_address
            
            # Process remainder of line if any
            remainder = remainder.strip()
            if remainder:
                self.process_line(remainder)
            return
        
        # Check for directive
        if line.startswith('.'):
            directive_match = re.match(r'(\.[a-zA-Z0-9_]+)\s*(.*)', line)
            if directive_match:
                directive, args = directive_match.groups()
                self.process_directive(directive, args)
            else:
                self.error(f"Invalid directive syntax: {line}")
            return
        
        # Must be an instruction
        if self.current_section != ".text":
            self.error(f"Instructions can only appear in .text section: {line}")
            return
        
        # Split instruction into opcode and operands
        parts = line.split(None, 1)
        opcode = parts[0].upper()
        operands = parts[1] if len(parts) > 1 else ""
        
        self.process_instruction(opcode, operands)
    
    def include_file(self, filename):
        """Process an included file."""
        # Check for circular includes
        abs_path = os.path.abspath(filename)
        if abs_path in self.included_files:
            self.error(f"Circular inclusion detected: {filename}")
            return
        
        # Mark file as included
        self.included_files.add(abs_path)
        
        # Save current file/line
        prev_file = self.current_file
        prev_line = self.current_line
        
        try:
            # Process the included file
            with open(filename, 'r') as f:
                self.current_file = filename
                for line_num, line in enumerate(f, 1):
                    self.current_line = line_num
                    self.process_line(line)
        except FileNotFoundError:
            self.error(f"Include file not found: {filename}")
        except Exception as e:
            self.error(f"Error processing include file: {str(e)}")
        
        # Restore file/line
        self.current_file = prev_file
        self.current_line = prev_line
        
        # Remove from included set to allow including again elsewhere
        self.included_files.remove(abs_path)
    
    def resolve_references(self):
        """Resolve forward references and late-bound symbols."""
        for index, symbol, ref_type in self.unresolved_references:
            if symbol in self.labels:
                # Get the instruction
                instruction = self.instructions[index]
                
                # Extract fields
                opcode = (instruction >> 24) & 0xFF
                mode = (instruction >> 20) & 0x0F
                reg1 = (instruction >> 16) & 0x0F
                
                # Get symbol value
                value = self.labels[symbol]
                
                # Rebuild the instruction with resolved value
                if ref_type == 'imm':
                    new_instruction = self.encode_instruction(opcode, mode, reg1, 0, value)
                    self.instructions[index] = new_instruction
            else:
                self.error(f"Unresolved symbol: {symbol}")
    
    def assemble(self, source_code, filename="<input>"):
        """Assemble the source code into binary."""
        # Reset state
        self.labels = {}
        self.instructions = []
        self.data = []
        self.current_section = ".text"
        self.address = CODE_SEGMENT_BASE
        self.data_address = DATA_SEGMENT_BASE
        self.unresolved_references = []
        self.errors = []
        self.current_file = filename
        self.included_files = set([os.path.abspath(filename)])
        
        # Process each line
        for line_num, line in enumerate(source_code.splitlines(), 1):
            self.current_line = line_num
            try:
                self.process_line(line)
            except Exception as e:
                self.error(f"Exception: {str(e)}")
        
        # Resolve forward references
        self.resolve_references()
        
        # Check for errors
        if self.errors:
            for error in self.errors:
                print(f"Error: {error}", file=sys.stderr)
            return None
        
        # Build the binary output
        binary = bytearray()
        
        # Add code segment
        for instruction in self.instructions:
            binary.extend(struct.pack("<I", instruction))
        
        # Pad to data segment if needed
        if self.data:
            code_size = len(binary)
            if code_size < DATA_SEGMENT_BASE:
                padding_size = DATA_SEGMENT_BASE - code_size
                binary.extend(bytes(padding_size))
            
            # Add data segment
            binary.extend(bytes(self.data))
        
        return binary
    
    def assemble_file(self, input_file, output_file=None):
        """Assemble an input file to binary."""
        try:
            # Determine output filename if not provided
            if not output_file:
                output_file = os.path.splitext(input_file)[0] + '.bin'
            
            # Read source code
            with open(input_file, 'r') as f:
                source_code = f.read()
            
            # Assemble the code
            binary = self.assemble(source_code, input_file)
            if not binary:
                print(f"Failed to assemble {input_file}", file=sys.stderr)
                return False
            
            # Write binary to output file
            with open(output_file, 'wb') as f:
                f.write(binary)
            
            # Report success
            code_size = len(self.instructions) * 4
            data_size = len(self.data)
            total_size = len(binary)
            
            print(f"Successfully assembled {input_file} to {output_file}")
            print(f"Code segment: {code_size} bytes ({len(self.instructions)} instructions)")
            print(f"Data segment: {data_size} bytes")
            print(f"Total binary size: {total_size} bytes")
            
            return True
        
        except Exception as e:
            print(f"Error: {str(e)}", file=sys.stderr)
            return False
    
    def disassemble(self, binary_data, start_address=0, show_addresses=True):
        """
        Basic disassembler for VM binary code.
        Returns a list of disassembled instructions.
        """
        result = []
        address = start_address
        
        # Process each 32-bit instruction
        for i in range(0, len(binary_data), 4):
            if i + 4 > len(binary_data):
                break
            
            # Extract the 32-bit instruction
            instruction = struct.unpack("<I", binary_data[i:i+4])[0]
            
            # Decode fields
            opcode = (instruction >> 24) & 0xFF
            mode = (instruction >> 20) & 0x0F
            reg1 = (instruction >> 16) & 0x0F
            reg2 = (instruction >> 12) & 0x0F
            immediate = instruction & 0xFFF
            
            # If mode is immediate-related, combine reg2 and immediate
            if mode in [AddressingMode.IMM, AddressingMode.MEM, AddressingMode.STK, AddressingMode.BAS]:
                immediate = (reg2 << 12) | immediate
            
            # Find opcode name
            opcode_name = "???"
            for name, value in self.opcodes.items():
                if value == opcode:
                    opcode_name = name
                    break
            
            # Format the instruction based on opcode and mode
            if opcode_name in ["NOP", "PUSHF", "POPF", "PUSHA", "POPA", "LEAVE", "HALT", "CLI", "STI", "IRET", "CPUID", "RESET", "DEBUG"]:
                # No operands
                disasm = f"{opcode_name}"
            
            elif opcode_name in ["INC", "DEC", "NEG", "NOT", "POP"]:
                # Single register operand
                disasm = f"{opcode_name} R{reg1}"
            
            elif opcode_name == "RET":
                # Optional immediate
                if immediate == 0:
                    disasm = f"{opcode_name}"
                else:
                    disasm = f"{opcode_name} #{immediate}"
            
            elif opcode_name == "PUSH":
                # Register or immediate
                if mode == AddressingMode.IMM:
                    disasm = f"{opcode_name} #{immediate}"
                else:
                    disasm = f"{opcode_name} R{reg1}"
            
            elif opcode_name in ["JMP", "JZ", "JNZ", "JN", "JP", "JO", "JC", "JBE", "JA", "CALL"]:
                # Jump target
                if mode == AddressingMode.IMM:
                    disasm = f"{opcode_name} #{immediate}"
                elif mode == AddressingMode.REG:
                    disasm = f"{opcode_name} R{reg1}"
                elif mode == AddressingMode.REGM:
                    disasm = f"{opcode_name} [R{reg1}]"
                elif mode == AddressingMode.IDX:
                    disasm = f"{opcode_name} [R{reg1}+{immediate}]"
                else:
                    disasm = f"{opcode_name} ???"
            
            elif opcode_name in ["ENTER", "INT", "SYSCALL"]:
                # Immediate value
                disasm = f"{opcode_name} #{immediate}"
            
            elif opcode_name == "MOVE":
                # Register to register
                disasm = f"{opcode_name} R{reg1}, R{reg2}"
            
            elif opcode_name in ["LOAD", "LOADB", "LOADW", "LEA", "ADD", "SUB", "MUL", "DIV", "MOD", 
                               "AND", "OR", "XOR", "SHL", "SHR", "SAR", "ROL", "ROR", "TEST", "CMP",
                               "ADDC", "SUBC"]:
                # Two operands, first is register
                if mode == AddressingMode.IMM:
                    disasm = f"{opcode_name} R{reg1}, #{immediate}"
                elif mode == AddressingMode.REG:
                    disasm = f"{opcode_name} R{reg1}, R{reg2}"
                elif mode == AddressingMode.MEM:
                    disasm = f"{opcode_name} R{reg1}, [{immediate}]"
                elif mode == AddressingMode.REGM:
                    disasm = f"{opcode_name} R{reg1}, [R{reg2}]"
                elif mode == AddressingMode.IDX:
                    disasm = f"{opcode_name} R{reg1}, [R{reg2}+{immediate}]"
                elif mode == AddressingMode.STK:
                    disasm = f"{opcode_name} R{reg1}, [SP+{immediate}]"
                elif mode == AddressingMode.BAS:
                    disasm = f"{opcode_name} R{reg1}, [BP+{immediate}]"
                else:
                    disasm = f"{opcode_name} R{reg1}, ???"
            
            elif opcode_name in ["STORE", "STOREB", "STOREW"]:
                # Store operations
                if mode == AddressingMode.MEM:
                    disasm = f"{opcode_name} R{reg1}, [{immediate}]"
                elif mode == AddressingMode.REGM:
                    disasm = f"{opcode_name} R{reg1}, [R{reg2}]"
                elif mode == AddressingMode.IDX:
                    disasm = f"{opcode_name} R{reg1}, [R{reg2}+{immediate}]"
                elif mode == AddressingMode.STK:
                    disasm = f"{opcode_name} R{reg1}, [SP+{immediate}]"
                elif mode == AddressingMode.BAS:
                    disasm = f"{opcode_name} R{reg1}, [BP+{immediate}]"
                else:
                    disasm = f"{opcode_name} R{reg1}, ???"
            
            else:
                # Unknown or complicated format
                disasm = f"{opcode_name} ???"
            
            # Add address prefix if requested
            if show_addresses:
                disasm = f"{address:04X}: {disasm}"
            
            result.append(disasm)
            address += 4
        
        return result

# Main function for CLI
def main():
    import argparse
    
    parser = argparse.ArgumentParser(description='Assembler for Virtual Machine')
    parser.add_argument('input', help='Input assembly file')
    parser.add_argument('-o', '--output', help='Output binary file (default: input file with .bin extension)')
    parser.add_argument('-v', '--verbose', action='store_true', help='Show verbose output')
    parser.add_argument('-d', '--disassemble', action='store_true', help='Disassemble the output after assembly')
    parser.add_argument('-l', '--list-file', help='Generate a listing file with assembled code')
    
    args = parser.parse_args()
    
    assembler = Assembler()
    
    success = assembler.assemble_file(args.input, args.output)
    
    if success and args.verbose:
        # Print symbol table
        print("\nSymbol Table:")
        for symbol, value in sorted(assembler.labels.items()):
            print(f"{symbol}: 0x{value:04X}")
    
    if success and args.disassemble:
        output_file = args.output or os.path.splitext(args.input)[0] + '.bin'
        with open(output_file, 'rb') as f:
            binary_data = f.read()
        
        print("\nDisassembly:")
        disassembly = assembler.disassemble(binary_data)
        for line in disassembly:
            print(line)
    
    if success and args.list_file:
        # Generate listing file with binary and source
        try:
            with open(args.input, 'r') as src_file, open(args.list_file, 'w') as list_file:
                list_file.write("Source Code:\n")
                list_file.write(src_file.read())
                list_file.write("\n\n")
                
                list_file.write("Disassembly:\n")
                disassembly = assembler.disassemble(binary_data)
                for line in disassembly:
                    list_file.write(line + "\n")
                
                print(f"Listing file generated: {args.list_file}")
        except Exception as e:
            print(f"Error generating listing file: {str(e)}", file=sys.stderr)


if __name__ == '__main__':
    main()