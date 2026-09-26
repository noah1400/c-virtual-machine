#include <ctype.h>
#include <string.h>
#include "instruction_set.h"

static const InstructionInfo instruction_table[] = {
    { "NOP",     NOP_OP,     FMT_NONE,         0 },
    { "LOAD",    LOAD_OP,    FMT_REG_OPERAND,  MODES_SRC },
    { "STORE",   STORE_OP,   FMT_REG_OPERAND,  MODES_ADDR },
    { "MOVE",    MOVE_OP,    FMT_REG_REG,      MODE_BIT(REG_MODE) },
    { "LOADB",   LOADB_OP,   FMT_REG_OPERAND,  MODES_SRC },
    { "STOREB",  STOREB_OP,  FMT_REG_OPERAND,  MODES_ADDR },
    { "LOADW",   LOADW_OP,   FMT_REG_OPERAND,  MODES_SRC },
    { "STOREW",  STOREW_OP,  FMT_REG_OPERAND,  MODES_ADDR },
    { "LEA",     LEA_OP,     FMT_REG_OPERAND,  MODES_ADDR },

    { "ADD",     ADD_OP,     FMT_REG_OPERAND,  MODES_SRC },
    { "SUB",     SUB_OP,     FMT_REG_OPERAND,  MODES_SRC },
    { "MUL",     MUL_OP,     FMT_REG_OPERAND,  MODES_SRC },
    { "DIV",     DIV_OP,     FMT_REG_OPERAND,  MODES_SRC },
    { "MOD",     MOD_OP,     FMT_REG_OPERAND,  MODES_SRC },
    { "INC",     INC_OP,     FMT_REG,          0 },
    { "DEC",     DEC_OP,     FMT_REG,          0 },
    { "NEG",     NEG_OP,     FMT_REG,          0 },
    { "CMP",     CMP_OP,     FMT_REG_OPERAND,  MODES_SRC },
    { "ADDC",    ADDC_OP,    FMT_REG_OPERAND,  MODES_SRC },
    { "SUBC",    SUBC_OP,    FMT_REG_OPERAND,  MODES_SRC },
    { "IDIV",    IDIV_OP,    FMT_REG_OPERAND,  MODES_SRC },
    { "IMOD",    IMOD_OP,    FMT_REG_OPERAND,  MODES_SRC },

    { "AND",     AND_OP,     FMT_REG_OPERAND,  MODES_SRC },
    { "OR",      OR_OP,      FMT_REG_OPERAND,  MODES_SRC },
    { "XOR",     XOR_OP,     FMT_REG_OPERAND,  MODES_SRC },
    { "NOT",     NOT_OP,     FMT_REG,          0 },
    { "SHL",     SHL_OP,     FMT_REG_OPERAND,  MODES_SRC },
    { "SHR",     SHR_OP,     FMT_REG_OPERAND,  MODES_SRC },
    { "SAR",     SAR_OP,     FMT_REG_OPERAND,  MODES_SRC },
    { "ROL",     ROL_OP,     FMT_REG_OPERAND,  MODES_SRC },
    { "ROR",     ROR_OP,     FMT_REG_OPERAND,  MODES_SRC },
    { "TEST",    TEST_OP,    FMT_REG_OPERAND,  MODES_SRC },

    { "JMP",     JMP_OP,     FMT_OPERAND,      MODES_SRC },
    { "JZ",      JZ_OP,      FMT_OPERAND,      MODES_SRC },
    { "JNZ",     JNZ_OP,     FMT_OPERAND,      MODES_SRC },
    { "JN",      JN_OP,      FMT_OPERAND,      MODES_SRC },
    { "JP",      JP_OP,      FMT_OPERAND,      MODES_SRC },
    { "JO",      JO_OP,      FMT_OPERAND,      MODES_SRC },
    { "JC",      JC_OP,      FMT_OPERAND,      MODES_SRC },
    { "JBE",     JBE_OP,     FMT_OPERAND,      MODES_SRC },
    { "JA",      JA_OP,      FMT_OPERAND,      MODES_SRC },
    { "CALL",    CALL_OP,    FMT_OPERAND,      MODES_SRC },
    { "RET",     RET_OP,     FMT_OPT_IMM,      MODE_BIT(IMM_MODE) },
    { "SYSCALL", SYSCALL_OP, FMT_IMM,          MODE_BIT(IMM_MODE) },
    { "LOOP",    LOOP_OP,    FMT_REG_OPERAND,  MODES_SRC },
    { "JL",      JL_OP,      FMT_OPERAND,      MODES_SRC },
    { "JGE",     JGE_OP,     FMT_OPERAND,      MODES_SRC },
    { "JLE",     JLE_OP,     FMT_OPERAND,      MODES_SRC },
    { "JG",      JG_OP,      FMT_OPERAND,      MODES_SRC },
    { "JAE",     JAE_OP,     FMT_OPERAND,      MODES_SRC },

    { "PUSH",    PUSH_OP,    FMT_OPERAND,      MODES_SRC },
    { "POP",     POP_OP,     FMT_REG,          0 },
    { "PUSHF",   PUSHF_OP,   FMT_NONE,         0 },
    { "POPF",    POPF_OP,    FMT_NONE,         0 },
    { "PUSHA",   PUSHA_OP,   FMT_NONE,         0 },
    { "POPA",    POPA_OP,    FMT_NONE,         0 },
    { "ENTER",   ENTER_OP,   FMT_IMM,          MODE_BIT(IMM_MODE) },
    { "LEAVE",   LEAVE_OP,   FMT_NONE,         0 },

    { "HALT",    HALT_OP,    FMT_NONE,         0 },
    { "INT",     INT_OP,     FMT_IMM,          MODE_BIT(IMM_MODE) },
    { "CLI",     CLI_OP,     FMT_NONE,         0 },
    { "STI",     STI_OP,     FMT_NONE,         0 },
    { "IRET",    IRET_OP,    FMT_NONE,         0 },
    { "IN",      IN_OP,      FMT_REG_OPERAND,  MODES_IMM_REG },
    { "OUT",     OUT_OP,     FMT_OPERAND_REG,  MODES_IMM_REG },
    { "CPUID",   CPUID_OP,   FMT_NONE,         0 },
    { "RESET",   RESET_OP,   FMT_NONE,         0 },
    { "DEBUG",   DEBUG_OP,   FMT_NONE,         0 },

    { "ALLOC",   ALLOC_OP,   FMT_REG_OPERAND,  MODES_IMM_REG },
    { "FREE",    FREE_OP,    FMT_REG,          0 },
    { "MEMCPY",  MEMCPY_OP,  FMT_REG_REG_SIZE, MODES_IMM_REG },
    { "MEMSET",  MEMSET_OP,  FMT_REG_REG_SIZE, MODES_IMM_REG },
    { "PROTECT", PROTECT_OP, FMT_REG_OPERAND,  MODES_IMM_REG },
};

#define INSTRUCTION_COUNT (sizeof(instruction_table) / sizeof(instruction_table[0]))

static const char *const register_names[16] = {
    "R0", "BP", "SP", "PC", "SR", "R5", "R6", "R7",
    "R8", "R9", "R10", "R11", "R12", "R13", "R14", "LR"
};

static const struct {
    const char *name;
    uint8_t index;
} register_aliases[] = {
    { "ACC", 0 }, { "R0_ACC", 0 }, { "BP", 1 }, { "R1_BP", 1 }, { "SP", 2 }, { "R2_SP", 2 },
    { "PC", 3 }, { "R3_PC", 3 }, { "SR", 4 }, { "R4_SR", 4 }, { "LR", 15 }, { "R15_LR", 15 },
};

static int equals_ignore_case(const char *a, const char *b) {
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

const InstructionInfo *isa_by_opcode(uint8_t opcode) {
    for (size_t i = 0; i < INSTRUCTION_COUNT; i++) {
        if (instruction_table[i].opcode == opcode) {
            return &instruction_table[i];
        }
    }
    return NULL;
}

const InstructionInfo *isa_by_mnemonic(const char *mnemonic) {
    for (size_t i = 0; i < INSTRUCTION_COUNT; i++) {
        if (equals_ignore_case(instruction_table[i].mnemonic, mnemonic)) {
            return &instruction_table[i];
        }
    }
    return NULL;
}

const InstructionInfo *isa_table(size_t *count) {
    *count = INSTRUCTION_COUNT;
    return instruction_table;
}

int isa_register_index(const char *name) {
    if ((name[0] == 'R' || name[0] == 'r') && isdigit((unsigned char)name[1])) {
        int index = 0;
        const char *p = name + 1;
        while (isdigit((unsigned char)*p) && index < 100) {
            index = index * 10 + (*p++ - '0');
        }
        if (*p == '\0' && index < 16 && !(name[1] == '0' && name[2] != '\0')) {
            return index;
        }
    }
    for (size_t i = 0; i < sizeof(register_aliases) / sizeof(register_aliases[0]); i++) {
        if (equals_ignore_case(register_aliases[i].name, name)) {
            return register_aliases[i].index;
        }
    }
    return -1;
}

const char *isa_register_name(uint8_t reg) {
    return register_names[reg & 0x0F];
}

const char *isa_mode_name(uint8_t mode) {
    static const char *const names[] = { "IMM", "REG", "MEM", "REGM", "IDX", "STK", "BAS" };
    return mode < sizeof(names) / sizeof(names[0]) ? names[mode] : "???";
}

int isa_mode_has_wide_immediate(uint8_t mode) {
    return mode == IMM_MODE || mode == MEM_MODE || mode == STK_MODE || mode == BAS_MODE;
}

// Signed offset of IDX (12-bit), STK and BAS (16-bit) operands
int32_t isa_displacement(const Instruction *instr) {
    switch (instr->mode) {
        case IDX_MODE:
            return (int32_t)((instr->immediate & 0x0FFF) ^ 0x0800) - 0x0800;
        case STK_MODE:
        case BAS_MODE:
            return (int32_t)(instr->immediate ^ 0x8000) - 0x8000;
        default:
            return 0;
    }
}

uint32_t isa_encode(const Instruction *instr) {
    uint32_t high = isa_mode_has_wide_immediate(instr->mode) ? (instr->immediate >> 12) & 0x0F
                                                             : instr->reg2 & 0x0F;
    return ((uint32_t)instr->opcode << 24) |
           ((uint32_t)(instr->mode & 0x0F) << 20) |
           ((uint32_t)(instr->reg1 & 0x0F) << 16) |
           (high << 12) |
           (instr->immediate & 0x0FFF);
}

void isa_decode(uint32_t word, Instruction *instr) {
    instr->opcode = (uint8_t)(word >> 24);
    instr->mode = (uint8_t)((word >> 20) & 0x0F);
    instr->reg1 = (uint8_t)((word >> 16) & 0x0F);
    instr->reg2 = (uint8_t)((word >> 12) & 0x0F);
    instr->immediate = (uint16_t)(word & 0x0FFF);
    if (isa_mode_has_wide_immediate(instr->mode)) {
        instr->immediate |= (uint16_t)(instr->reg2 << 12);
    }
}
