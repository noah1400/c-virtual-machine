#ifndef _INSTRUCTION_SET_H_
#define _INSTRUCTION_SET_H_

#include <stdint.h>

// Instruction format
// [Opcode: 8 bits][Mode: 4 bits][Reg1: 4 bits][Reg2: 4 bits][Immediate/Offset: 12 bits]

// Addressing modes
#define IMM_MODE (uint8_t)0x0 // Immediate: Value in instruction
#define REG_MODE (uint8_t)0x1 // Register: Value in register
#define MEM_MODE (uint8_t)0x2  // Memory: Direct address
#define REGM_MODE (uint8_t)0x3 // Register Indirect: [Reg]
#define IDX_MODE (uint8_t)0x4  // Indexed: [Reg + Offset]
#define STK_MODE (uint8_t)0x5  // Stack Relative: [SP + Offset]
#define BAS_MODE (uint8_t)0x6  // Base Relative: [BP + Offset]

// Data Transfer Instructions (0x00-0x1F)
// OP | Operands | Description | Flags
#define NOP_OP      (uint8_t)0x00 // NOP | - | No Operation | None
#define LOAD_OP     (uint8_t)0x01 // LOAD | Reg, Src | Load value into register | None
#define STORE_OP    (uint8_t)0x02 // STORE | Src, Dest | Store value to memory | None
#define MOVE_OP     (uint8_t)0x03 // MOVE | SrcReg, DstReg | Copy register to register | None
#define LOADB_OP    (uint8_t)0x04 // LOADB | Reg, Src | Load byte into register | None
#define STOREB_OP   (uint8_t)0x05 // STOREB | Src, Dest | Store byte to memory | None
#define LOADW_OP    (uint8_t)0x06 // LOADW | Reg, Src | Load word (16-bit) into register | None
#define STOREW_OP   (uint8_t)0x07 // STOREW | Src, Dest | Store word (16-bit) to memory | None
#define LEA_OP      (uint8_t)0x08 // LEA | Reg, Src | Load effective address into register | None

// Arithmetic Instructions (0x20-0x3F)
#define ADD_OP      (uint8_t)0x20 // ADD | Reg1, Reg2/Imm | Add to register | Z, N, C, O
#define SUB_OP      (uint8_t)0x21 // SUB | Reg1, Reg2/Imm | Subtract from register | Z, N, C, O
#define MUL_OP      (uint8_t)0x22 // MUL | Reg1, Reg2/Imm | Multiply register | Z, N, O
#define DIV_OP      (uint8_t)0x23 // DIV | Reg1, Reg2/Imm | Divide register | Z, N
#define MOD_OP      (uint8_t)0x24 // MOD | Reg1, Reg2/Imm | Modulo operation | Z, N
#define INC_OP      (uint8_t)0x25 // INC | Reg | Increment register | Z, N, O
#define DEC_OP      (uint8_t)0x26 // DEC | Reg | Decrement register | Z, N, O
#define NEG_OP      (uint8_t)0x27 // NEG | Reg | Negate register | Z, N
#define CMP_OP      (uint8_t)0x28 // CMP | Reg1, Reg2/Imm | Compare values (set flags) | Z, N, C, O
#define ADDC_OP     (uint8_t)0x2A // ADDC | Reg1, Reg2/Imm | Add with carry | Z, N, C, O
#define SUBC_OP     (uint8_t)0x2B // SUBC | Reg1, Reg2/Imm | Subtract with carry | Z, N, C, O

// Logical Instructions (0x40-0x5F)
#define AND_OP      (uint8_t)0x40 // AND | Reg1, Reg2/Imm | Bitwise AND | Z, N
#define OR_OP       (uint8_t)0x41 // OR | Reg1, Reg2/Imm | Bitwise OR | Z, N
#define XOR_OP      (uint8_t)0x42 // XOR | Reg1, Reg2/Imm | Bitwise XOR | Z, N
#define NOT_OP      (uint8_t)0x43 // NOT | Reg | Bitwise NOT | Z, N
#define SHL_OP      (uint8_t)0x44 // SHL | Reg, Count | Shift left | Z, N, C
#define SHR_OP      (uint8_t)0x45 // SHR | Reg, Count | Shift right (logical) | Z, N, C
#define SAR_OP      (uint8_t)0x46 // SAR | Reg, Count | Shift right (arithmetic) | Z, N, C
#define ROL_OP      (uint8_t)0x47 // ROL | Reg, Count | Rotate left | C
#define ROR_OP      (uint8_t)0x48 // ROR | Reg, Count | Rotate right | C
#define TEST_OP     (uint8_t)0x49 // TEST | Reg1, Reg2/Imm | Test bits (AND without store) | Z, N

// Control Flow Instructions (0x60-0x7F)
#define JMP_OP      (uint8_t)0x60 // JMP | Target | Unconditional jump | None
#define JZ_OP       (uint8_t)0x61 // JZ | Target | Jump if zero | None
#define JNZ_OP      (uint8_t)0x62 // JNZ | Target | Jump if not zero | None
#define JN_OP       (uint8_t)0x63 // JN | Target | Jump if negative | None
#define JP_OP       (uint8_t)0x64 // JP | Target | Jump if positive | None
#define JO_OP       (uint8_t)0x65 // JO | Target | Jump if overflow | None
#define JC_OP       (uint8_t)0x66 // JC | Target | Jump if carry | None
#define JBE_OP      (uint8_t)0x67 // JBE | Target | Jump if below or equal | None
#define JA_OP       (uint8_t)0x68 // JA | Target | Jump if above | None
#define CALL_OP     (uint8_t)0x6A // CALL | Target | Call subroutine | None
#define RET_OP      (uint8_t)0x6B // RET | - | Return from subroutine | None
#define SYSCALL_OP  (uint8_t)0x6C // SYSCALL | Number | System call | Varies
#define LOOP_OP     (uint8_t)0x6F // LOOP | Reg, Target | Decrement and jump if not zero | None

// Stack Instructions (0x80-0x9F)
#define PUSH_OP     (uint8_t)0x80 // PUSH | Reg/Imm | Push value onto stack | None
#define POP_OP      (uint8_t)0x81 // POP | Reg | Pop value from stack | None
#define PUSHF_OP    (uint8_t)0x82 // PUSHF | - | Push flags onto stack | None
#define POPF_OP     (uint8_t)0x83 // POPF | - | Pop flags from stack | All
#define PUSHA_OP    (uint8_t)0x84 // PUSHA | - | Push all registers | None
#define POPA_OP     (uint8_t)0x85 // POPA | - | Pop all registers | None
#define ENTER_OP    (uint8_t)0x86 // ENTER | Size | Create stack frame | None
#define LEAVE_OP    (uint8_t)0x87 // LEAVE | - | Destroy stack frame | None

// System Instructions (0xA0-0xBF)
#define HALT_OP     (uint8_t)0xA0 // HALT | - | Halt execution | None
#define INT_OP      (uint8_t)0xA1 // INT | Vector | Generate interrupt | Varies
#define CLI_OP      (uint8_t)0xA2 // CLI | - | Clear interrupt flag | I
#define STI_OP      (uint8_t)0xA3 // STI | - | Set interrupt flag | I
#define IRET_OP     (uint8_t)0xA4 // IRET | - | Return from interrupt | All
#define IN_OP       (uint8_t)0xA5 // IN | Reg, Port | Input from I/O port | None
#define OUT_OP      (uint8_t)0xA6 // OUT | Port, Reg/Imm | Output to I/O port | None
#define CPUID_OP    (uint8_t)0xA7 // CPUID | - | Get CPU information | None
#define RESET_OP    (uint8_t)0xA8 // RESET | - | Reset VM | All
#define DEBUG_OP    (uint8_t)0xA9 // DEBUG | - | Trigger debugger | None

// Memory Control Instructions (0xC0-0xDF)
#define ALLOC_OP    (uint8_t)0xC0 // ALLOC | Reg, Size | Allocate heap memory | None
#define FREE_OP     (uint8_t)0xC1 // FREE | Reg | Free heap memory | None
#define MEMCPY_OP   (uint8_t)0xC2 // MEMCPY | Dst, Src, Size | Copy memory block | None
#define MEMSET_OP   (uint8_t)0xC3 // MEMSET | Dst, Val, Size | Set memory block | None
#define PROTECT_OP  (uint8_t)0xC4 // PROTECT | Addr, Flags | Set memory protection | None

// Flage Definitions ( Status Register )
#define ZERO_FLAG   (uint8_t)0b00000001 // Zero flag
#define NEG_FLAG    (uint8_t)0b00000010 // Negative flag
#define CARRY_FLAG  (uint8_t)0b00000100 // Carry flag
#define OVER_FLAG   (uint8_t)0b00001000 // Overflow flag
#define INT_FLAG    (uint8_t)0b00010000 // Interrupt enable flag
#define DIR_FLAG    (uint8_t)0b00100000 // Direction flag
#define SYS_FLAG    (uint8_t)0b01000000 // System mode flag
#define TRAP_FLAG   (uint8_t)0b10000000 // Trap flag (debug)
#endif // _INSTRUCTION_SET_H_