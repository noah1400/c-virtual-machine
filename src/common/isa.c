#include <ctype.h>
#include <string.h>
#include "instruction_set.h"

static const InstructionInfo instruction_table[] = {
    { "NOP",     NOP_OP,     FMT_NONE,         0,                  0 },
    { "LOAD",    LOAD_OP,    FMT_REG_OPERAND,  MODES_SRC,          0 },
    { "STORE",   STORE_OP,   FMT_REG_OPERAND,  MODES_ADDR,         0 },
    { "MOVE",    MOVE_OP,    FMT_REG_REG,      MODE_BIT(REG_MODE), 0 },
    { "LOADB",   LOADB_OP,   FMT_REG_OPERAND,  MODES_SRC,          0 },
    { "STOREB",  STOREB_OP,  FMT_REG_OPERAND,  MODES_ADDR,         0 },
    { "LOADW",   LOADW_OP,   FMT_REG_OPERAND,  MODES_SRC,          0 },
    { "STOREW",  STOREW_OP,  FMT_REG_OPERAND,  MODES_ADDR,         0 },
    { "LEA",     LEA_OP,     FMT_REG_OPERAND,  MODES_ADDR,         0 },

    { "ADD",     ADD_OP,     FMT_REG_OPERAND,  MODES_SRC,          0 },
    { "SUB",     SUB_OP,     FMT_REG_OPERAND,  MODES_SRC,          0 },
    { "MUL",     MUL_OP,     FMT_REG_OPERAND,  MODES_SRC,          0 },
    { "DIV",     DIV_OP,     FMT_REG_OPERAND,  MODES_SRC,          0 },
    { "MOD",     MOD_OP,     FMT_REG_OPERAND,  MODES_SRC,          0 },
    { "INC",     INC_OP,     FMT_REG,          0,                  0 },
    { "DEC",     DEC_OP,     FMT_REG,          0,                  0 },
    { "NEG",     NEG_OP,     FMT_REG,          0,                  0 },
    { "CMP",     CMP_OP,     FMT_REG_OPERAND,  MODES_SRC,          0 },
    { "ADDC",    ADDC_OP,    FMT_REG_OPERAND,  MODES_SRC,          0 },
    { "SUBC",    SUBC_OP,    FMT_REG_OPERAND,  MODES_SRC,          0 },
    { "IDIV",    IDIV_OP,    FMT_REG_OPERAND,  MODES_SRC,          0 },
    { "IMOD",    IMOD_OP,    FMT_REG_OPERAND,  MODES_SRC,          0 },

    { "AND",     AND_OP,     FMT_REG_OPERAND,  MODES_SRC,          0 },
    { "OR",      OR_OP,      FMT_REG_OPERAND,  MODES_SRC,          0 },
    { "XOR",     XOR_OP,     FMT_REG_OPERAND,  MODES_SRC,          0 },
    { "NOT",     NOT_OP,     FMT_REG,          0,                  0 },
    { "SHL",     SHL_OP,     FMT_REG_OPERAND,  MODES_SRC,          0 },
    { "SHR",     SHR_OP,     FMT_REG_OPERAND,  MODES_SRC,          0 },
    { "SAR",     SAR_OP,     FMT_REG_OPERAND,  MODES_SRC,          0 },
    { "ROL",     ROL_OP,     FMT_REG_OPERAND,  MODES_SRC,          0 },
    { "ROR",     ROR_OP,     FMT_REG_OPERAND,  MODES_SRC,          0 },
    { "TEST",    TEST_OP,    FMT_REG_OPERAND,  MODES_SRC,          0 },

    { "JMP",     JMP_OP,     FMT_OPERAND,      MODES_SRC,          0 },
    { "JZ",      JZ_OP,      FMT_OPERAND,      MODES_SRC,          0 },
    { "JNZ",     JNZ_OP,     FMT_OPERAND,      MODES_SRC,          0 },
    { "JN",      JN_OP,      FMT_OPERAND,      MODES_SRC,          0 },
    { "JP",      JP_OP,      FMT_OPERAND,      MODES_SRC,          0 },
    { "JO",      JO_OP,      FMT_OPERAND,      MODES_SRC,          0 },
    { "JC",      JC_OP,      FMT_OPERAND,      MODES_SRC,          0 },
    { "JBE",     JBE_OP,     FMT_OPERAND,      MODES_SRC,          0 },
    { "JA",      JA_OP,      FMT_OPERAND,      MODES_SRC,          0 },
    { "CALL",    CALL_OP,    FMT_OPERAND,      MODES_SRC,          0 },
    { "RET",     RET_OP,     FMT_OPT_IMM,      MODE_BIT(IMM_MODE), 0 },
    { "SYSCALL", SYSCALL_OP, FMT_IMM,          MODE_BIT(IMM_MODE), 1 },
    { "LOOP",    LOOP_OP,    FMT_REG_OPERAND,  MODES_SRC,          0 },
    { "JL",      JL_OP,      FMT_OPERAND,      MODES_SRC,          0 },
    { "JGE",     JGE_OP,     FMT_OPERAND,      MODES_SRC,          0 },
    { "JLE",     JLE_OP,     FMT_OPERAND,      MODES_SRC,          0 },
    { "JG",      JG_OP,      FMT_OPERAND,      MODES_SRC,          0 },
    { "JAE",     JAE_OP,     FMT_OPERAND,      MODES_SRC,          0 },

    { "PUSH",    PUSH_OP,    FMT_OPERAND,      MODES_SRC,          0 },
    { "POP",     POP_OP,     FMT_REG,          0,                  0 },
    { "PUSHF",   PUSHF_OP,   FMT_NONE,         0,                  0 },
    { "POPF",    POPF_OP,    FMT_NONE,         0,                  0 },
    { "PUSHA",   PUSHA_OP,   FMT_NONE,         0,                  0 },
    { "POPA",    POPA_OP,    FMT_NONE,         0,                  0 },
    { "ENTER",   ENTER_OP,   FMT_IMM,          MODE_BIT(IMM_MODE), 0 },
    { "LEAVE",   LEAVE_OP,   FMT_NONE,         0,                  0 },

    { "HALT",    HALT_OP,    FMT_NONE,         0,                  1 },
    { "INT",     INT_OP,     FMT_IMM,          MODE_BIT(IMM_MODE), 0 },
    { "CLI",     CLI_OP,     FMT_NONE,         0,                  1 },
    { "STI",     STI_OP,     FMT_NONE,         0,                  1 },
    { "IRET",    IRET_OP,    FMT_NONE,         0,                  1 },
    { "IN",      IN_OP,      FMT_REG_OPERAND,  MODES_IMM_REG,      1 },
    { "OUT",     OUT_OP,     FMT_OPERAND_REG,  MODES_IMM_REG,      1 },
    { "CPUID",   CPUID_OP,   FMT_NONE,         0,                  0 },
    { "RESET",   RESET_OP,   FMT_NONE,         0,                  1 },
    { "DEBUG",   DEBUG_OP,   FMT_NONE,         0,                  0 },
    { "MFCR",    MFCR_OP,    FMT_REG_CTRL,     MODE_BIT(IMM_MODE), 1 },
    { "MTCR",    MTCR_OP,    FMT_CTRL_REG,     MODE_BIT(IMM_MODE), 1 },

    { "ALLOC",   ALLOC_OP,   FMT_REG_OPERAND,  MODES_IMM_REG,      0 },
    { "FREE",    FREE_OP,    FMT_REG,          0,                  0 },
    { "MEMCPY",  MEMCPY_OP,  FMT_REG_REG_SIZE, MODES_IMM_REG,      0 },
    { "MEMSET",  MEMSET_OP,  FMT_REG_REG_SIZE, MODES_IMM_REG,      0 },
    { "PROTECT", PROTECT_OP, FMT_REG_OPERAND,  MODES_IMM_REG,      1 },
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

static const char *const control_register_names[CR_COUNT] = {
    "IVTB", "KSP", "PTB", "FADDR", "ECODE", "SLO", "SHI", "HEAPLO", "HEAPHI"
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

// Called for every executed instruction, so opcodes are looked up in a table built on first use
const InstructionInfo *isa_by_opcode(uint8_t opcode) {
    static const InstructionInfo *by_opcode[256];
    static int ready;

    if (!ready) {
        for (size_t i = 0; i < INSTRUCTION_COUNT; i++) {
            by_opcode[instruction_table[i].opcode] = &instruction_table[i];
        }
        ready = 1;
    }
    return by_opcode[opcode];
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

int isa_has_relative_target(uint8_t opcode) {
    const InstructionInfo *info = isa_by_opcode(opcode);
    return info && (opcode >> 5) == (JMP_OP >> 5) && (info->format == FMT_OPERAND || info->format == FMT_REG_OPERAND);
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

int isa_control_register_index(const char *name) {
    for (int i = 0; i < CR_COUNT; i++) {
        if (equals_ignore_case(control_register_names[i], name)) {
            return i;
        }
    }
    return -1;
}

const char *isa_control_register_name(uint32_t index) {
    return index < CR_COUNT ? control_register_names[index] : "?";
}

const char *isa_mode_name(uint8_t mode) {
    static const char *const names[] = { "IMM", "REG", "MEM", "REGM", "IDX", "STK", "BAS" };
    return mode < sizeof(names) / sizeof(names[0]) ? names[mode] : "???";
}

// Operand fields of IMM, MEM, STK and BAS take 16 bits by borrowing the Reg2 field, except in
// MEMCPY and MEMSET, whose Reg2 field names the second register; only MEM is zero-extended
static int has_wide_field(const Instruction *instr) {
    int wide_mode = instr->mode == IMM_MODE || instr->mode == MEM_MODE || instr->mode == STK_MODE ||
                    instr->mode == BAS_MODE;
    return wide_mode && instr->opcode != MEMCPY_OP && instr->opcode != MEMSET_OP;
}

uint32_t isa_instruction_size(uint32_t word) {
    return (word >> 20) & MODE_EXTENDED ? 8 : 4;
}

void isa_decode(uint32_t word, uint32_t extension, Instruction *instr) {
    uint32_t field = word & 0x0FFF;

    instr->opcode = (uint8_t)(word >> 24);
    instr->mode = (uint8_t)((word >> 20) & 0x07);
    instr->extended = (uint8_t)((word >> 23) & 1);
    instr->reg1 = (uint8_t)((word >> 16) & 0x0F);
    instr->reg2 = (uint8_t)((word >> 12) & 0x0F);

    if (instr->extended) {
        instr->immediate = extension;
    } else if (has_wide_field(instr)) {
        field |= (uint32_t)instr->reg2 << 12;
        instr->reg2 = 0;
        instr->immediate = instr->mode == MEM_MODE ? field : (uint32_t)(int16_t)field;
    } else if (instr->mode == IDX_MODE) {
        instr->immediate = (uint32_t)(((int32_t)field ^ 0x800) - 0x800);
    } else {
        instr->immediate = field;
    }
}

int isa_encode(const Instruction *instr, uint32_t words[2]) {
    uint32_t mode = instr->mode | (instr->extended ? MODE_EXTENDED : 0);
    uint32_t reg2 = instr->reg2 & 0x0F, field = 0;

    if (!instr->extended) {
        field = instr->immediate & 0x0FFF;
        if (has_wide_field(instr)) {
            reg2 = (instr->immediate >> 12) & 0x0F;
        }
    }
    words[0] = ((uint32_t)instr->opcode << 24) | (mode << 20) | ((uint32_t)(instr->reg1 & 0x0F) << 16) |
               (reg2 << 12) | field;
    words[1] = instr->immediate;
    return instr->extended ? 2 : 1;
}
