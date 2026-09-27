#ifndef _INSTRUCTION_SET_H_
#define _INSTRUCTION_SET_H_

#include <stddef.h>
#include <stdint.h>

// Instruction format
// [Opcode: 8 bits][Mode: 4 bits][Reg1: 4 bits][Reg2: 4 bits][Immediate/Offset: 12 bits]
// IMM, MEM, STK and BAS modes use the Reg2 field as the top 4 bits of a 16-bit immediate, which
// is sign-extended except for MEM addresses.
// Mode bit 3 means a second word follows that holds the whole 32-bit immediate or offset.
#define MODE_EXTENDED 0x8

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
#define MOVE_OP     (uint8_t)0x03 // MOVE | DstReg, SrcReg | Copy register to register | None
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
#define NEG_OP      (uint8_t)0x27 // NEG | Reg | Negate register | Z, N, O
#define CMP_OP      (uint8_t)0x28 // CMP | Reg1, Reg2/Imm | Compare values (set flags) | Z, N, C, O
#define ADDC_OP     (uint8_t)0x2A // ADDC | Reg1, Reg2/Imm | Add with carry | Z, N, C, O
#define SUBC_OP     (uint8_t)0x2B // SUBC | Reg1, Reg2/Imm | Subtract with carry | Z, N, C, O
#define IDIV_OP     (uint8_t)0x2C // IDIV | Reg1, Reg2/Imm | Signed divide | Z, N
#define IMOD_OP     (uint8_t)0x2D // IMOD | Reg1, Reg2/Imm | Signed remainder | Z, N

// Logical Instructions (0x40-0x5F)
#define AND_OP      (uint8_t)0x40 // AND | Reg1, Reg2/Imm | Bitwise AND | Z, N, C and O cleared
#define OR_OP       (uint8_t)0x41 // OR | Reg1, Reg2/Imm | Bitwise OR | Z, N, C and O cleared
#define XOR_OP      (uint8_t)0x42 // XOR | Reg1, Reg2/Imm | Bitwise XOR | Z, N, C and O cleared
#define NOT_OP      (uint8_t)0x43 // NOT | Reg | Bitwise NOT | Z, N
#define SHL_OP      (uint8_t)0x44 // SHL | Reg, Count | Shift left | Z, N, C
#define SHR_OP      (uint8_t)0x45 // SHR | Reg, Count | Shift right (logical) | Z, N, C
#define SAR_OP      (uint8_t)0x46 // SAR | Reg, Count | Shift right (arithmetic) | Z, N, C
#define ROL_OP      (uint8_t)0x47 // ROL | Reg, Count | Rotate left | Z, N, C
#define ROR_OP      (uint8_t)0x48 // ROR | Reg, Count | Rotate right | Z, N, C
#define TEST_OP     (uint8_t)0x49 // TEST | Reg1, Reg2/Imm | Test bits (AND without store) | Z, N, C and O cleared

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
#define RET_OP      (uint8_t)0x6B // RET | [Imm] | Return from subroutine, then drop Imm bytes | None
#define SYSCALL_OP  (uint8_t)0x6C // SYSCALL | Number | System call | Varies
#define LOOP_OP     (uint8_t)0x6F // LOOP | Reg, Target | Decrement and jump if not zero | None
#define JL_OP       (uint8_t)0x70 // JL | Target | Jump if less (signed) | None
#define JGE_OP      (uint8_t)0x71 // JGE | Target | Jump if greater or equal (signed) | None
#define JLE_OP      (uint8_t)0x72 // JLE | Target | Jump if less or equal (signed) | None
#define JG_OP       (uint8_t)0x73 // JG | Target | Jump if greater (signed) | None
#define JAE_OP      (uint8_t)0x74 // JAE | Target | Jump if above or equal (unsigned) | None

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
#define INT_OP      (uint8_t)0xA1 // INT | Vector | Generate interrupt | I
#define CLI_OP      (uint8_t)0xA2 // CLI | - | Clear interrupt flag | I
#define STI_OP      (uint8_t)0xA3 // STI | - | Set interrupt flag | I
#define IRET_OP     (uint8_t)0xA4 // IRET | - | Return from interrupt | All
#define IN_OP       (uint8_t)0xA5 // IN | Reg, Port | Input from I/O port | None
#define OUT_OP      (uint8_t)0xA6 // OUT | Port, Reg | Output to I/O port | None
#define CPUID_OP    (uint8_t)0xA7 // CPUID | - | Get CPU information | None
#define RESET_OP    (uint8_t)0xA8 // RESET | - | Reset VM | All
#define DEBUG_OP    (uint8_t)0xA9 // DEBUG | - | Trigger debugger | None
#define MFCR_OP     (uint8_t)0xAA // MFCR | Reg, Ctrl | Read a control register | None
#define MTCR_OP     (uint8_t)0xAB // MTCR | Ctrl, Reg | Write a control register | None

// Memory Control Instructions (0xC0-0xDF)
#define ALLOC_OP    (uint8_t)0xC0 // ALLOC | Reg, Size | Allocate heap memory | None
#define FREE_OP     (uint8_t)0xC1 // FREE | Reg | Free heap memory | None
#define MEMCPY_OP   (uint8_t)0xC2 // MEMCPY | Dst, Src, Size | Copy memory block | None
#define MEMSET_OP   (uint8_t)0xC3 // MEMSET | Dst, Val, Size | Set memory block | None
#define PROTECT_OP  (uint8_t)0xC4 // PROTECT | Addr, Flags | Set memory protection | None

// Flag definitions (status register)
#define ZERO_FLAG   (uint8_t)0x01 // Zero flag
#define NEG_FLAG    (uint8_t)0x02 // Negative flag
#define CARRY_FLAG  (uint8_t)0x04 // Carry flag
#define OVER_FLAG   (uint8_t)0x08 // Overflow flag
#define INT_FLAG    (uint8_t)0x10 // Interrupt enable flag
#define DIR_FLAG    (uint8_t)0x20 // Direction flag
#define SYS_FLAG    (uint8_t)0x40 // System mode flag
#define TRAP_FLAG   (uint8_t)0x80 // Trap flag (debug)

// Control registers, read with MFCR and written with MTCR
#define CR_IVTB     0   // Interrupt vector table base, 0 when there is no table
#define CR_KSP      1   // Kernel stack pointer
#define CR_PTB      2   // Page table base
#define CR_FADDR    3   // Address of the last memory fault
#define CR_ECODE    4   // Details of the last page fault
#define CR_SLO      5   // Lowest address the stack may grow to
#define CR_SHI      6   // Highest stack address, where the stack starts
#define CR_HEAPLO   7   // Start of the heap
#define CR_HEAPHI   8   // End of the heap
#define CR_COUNT    9

// Instruction structure that represents a decoded instruction
typedef struct {
    uint8_t opcode;          // 8-bit opcode
    uint8_t mode;            // addressing mode without the extension bit
    uint8_t reg1;            // 4-bit register 1
    uint8_t reg2;            // 4-bit register 2
    uint8_t extended;        // the immediate came from a second word
    uint32_t immediate;      // operand value, offsets already sign-extended
} Instruction;

// Addressing mode sets accepted by an instruction's variable operand
#define MODE_BIT(mode)  (1u << (mode))
#define MODES_IMM_REG   (MODE_BIT(IMM_MODE) | MODE_BIT(REG_MODE))
#define MODES_ADDR      (MODE_BIT(MEM_MODE) | MODE_BIT(REGM_MODE) | MODE_BIT(IDX_MODE) | \
                         MODE_BIT(STK_MODE) | MODE_BIT(BAS_MODE))
#define MODES_SRC       (MODES_IMM_REG | MODES_ADDR)
#define MODES_WITH_IMMEDIATE (MODE_BIT(IMM_MODE) | MODE_BIT(MEM_MODE) | MODE_BIT(IDX_MODE) | \
                              MODE_BIT(STK_MODE) | MODE_BIT(BAS_MODE))

typedef enum {
    FMT_NONE,           // HALT
    FMT_REG,            // INC Rd
    FMT_IMM,            // SYSCALL #n
    FMT_OPT_IMM,        // RET [#n]
    FMT_OPERAND,        // JMP target, PUSH value (operand register in reg1)
    FMT_REG_OPERAND,    // LOAD Rd, source (operand register in reg2)
    FMT_OPERAND_REG,    // OUT port, Rs (operand register in reg2)
    FMT_REG_REG,        // MOVE Rd, Rs
    FMT_REG_REG_SIZE,   // MEMCPY Rd, Rs, #n | Rn (size register in the immediate field)
    FMT_REG_CTRL,       // MFCR Rd, IVTB (control register number in the immediate field)
    FMT_CTRL_REG        // MTCR IVTB, Rs
} OperandFormat;

typedef struct {
    const char *mnemonic;
    uint8_t opcode;
    uint8_t format;
    uint8_t modes;
} InstructionInfo;

const InstructionInfo *isa_by_opcode(uint8_t opcode);
const InstructionInfo *isa_by_mnemonic(const char *mnemonic);
const InstructionInfo *isa_table(size_t *count);

// Jumps, calls and LOOP, whose immediate operand is an offset from the next instruction
int isa_has_relative_target(uint8_t opcode);

int isa_register_index(const char *name);
const char *isa_register_name(uint8_t reg);
int isa_control_register_index(const char *name);
const char *isa_control_register_name(uint32_t index);
const char *isa_mode_name(uint8_t mode);

// Size in bytes of the instruction whose first word is given: 4, or 8 with an extension word
uint32_t isa_instruction_size(uint32_t word);
void isa_decode(uint32_t word, uint32_t extension, Instruction *instr);
// Returns the number of words written, 1 or 2
int isa_encode(const Instruction *instr, uint32_t words[2]);

#endif // _INSTRUCTION_SET_H_
