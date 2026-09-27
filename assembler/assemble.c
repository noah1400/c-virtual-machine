#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asm.h"
#include "vm_types.h"

typedef struct {
    uint8_t mode;
    uint8_t reg;
    int64_t value;
    int unresolved;
} Operand;

void asm_error(Assembler *as, const char *format, ...) {
    va_list args;

    // Layout passes repeat work that the final pass checks again
    if (as->pass == PASS_LAYOUT) {
        return;
    }
    if (as->line) {
        fprintf(stderr, "%s:%d: error: ", as->line->file, as->line->number);
    } else {
        fprintf(stderr, "vmasm: error: ");
    }
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    as->errors++;
}

void asm_init(Assembler *as) {
    memset(as, 0, sizeof(*as));
    as->sections[SECTION_TEXT] = (Section){ ".text", CODE_SEGMENT_BASE, CODE_SEGMENT_BASE + CODE_SEGMENT_SIZE,
                                            0, 0, NULL };
    as->sections[SECTION_DATA] = (Section){ ".data", DATA_SEGMENT_BASE, DATA_SEGMENT_BASE + DATA_SEGMENT_SIZE,
                                            0, 0, NULL };
    symbols_init(&as->symbols);
}

void asm_free(Assembler *as) {
    for (size_t i = 0; i < as->line_count; i++) {
        free(as->lines[i].text);
    }
    for (size_t i = 0; i < as->file_count; i++) {
        free(as->files[i]);
    }
    for (int i = 0; i < SECTION_COUNT; i++) {
        free(as->sections[i].bytes);
    }
    for (size_t i = 0; i < as->macro_count; i++) {
        Macro *macro = &as->macros[i];
        for (int k = 0; k < macro->param_count; k++) {
            free(macro->params[k]);
        }
        for (int k = 0; k < macro->body_count; k++) {
            free(macro->body[k]);
        }
        free(macro->name);
        free(macro->params);
        free(macro->body);
    }
    free(as->macros);
    free(as->lines);
    free(as->files);
    free(as->results);
    symbols_free(&as->symbols);
}

static Section *current(Assembler *as) {
    return &as->sections[as->section];
}

static Token *peek(Parser *p) {
    return &p->tokens->items[p->pos];
}

static int accept(Parser *p, int punct) {
    if (token_is_punct(peek(p), punct)) {
        p->pos++;
        return 1;
    }
    return 0;
}

static int expect_end(Parser *p) {
    if (peek(p)->kind != TOK_END) {
        if (!p->failed) {
            asm_error(p->as, "unexpected text after the statement");
        }
        p->failed = 1;
        return 0;
    }
    return 1;
}

// Evaluates an expression that decides the layout, so it must be known in pass 1 and keep its value
static int eval_now(Parser *p, int64_t *value, const char *what) {
    Assembler *as = p->as;
    LineResult *result = &as->results[as->line - as->lines];

    p->unresolved = 0;
    *value = parse_expression(p);
    if (p->failed) {
        return 0;
    }
    if (p->unresolved) {
        asm_error(as, "%s must not depend on symbols defined later", what);
        p->failed = 1;
        return 0;
    }
    if (as->pass == PASS_DEFINE) {
        result->layout_value = *value;
    } else if (*value != result->layout_value) {
        asm_error(as, "%s changes when the labels it uses move", what);
        *value = result->layout_value;
        p->failed = as->pass == PASS_EMIT;
        return !p->failed;
    }
    return 1;
}

static void emit(Assembler *as, const uint8_t *bytes, uint32_t count) {
    Section *sec = current(as);

    if (count > sec->limit - sec->pc) {
        asm_error(as, "%s section overflows its %u byte segment", sec->name, sec->limit - sec->base);
        sec->pc = sec->limit;
        return;
    }
    if (as->pass == PASS_EMIT && bytes) {
        memcpy(sec->bytes + (sec->pc - sec->base), bytes, count);
    }
    sec->pc += count;
    if (sec->pc > sec->end) {
        sec->end = sec->pc;
    }
}

static void emit_fill(Assembler *as, uint8_t value, uint32_t count) {
    Section *sec = current(as);
    uint32_t start = sec->pc;

    emit(as, NULL, count);
    if (as->pass == PASS_EMIT && sec->pc > start) {
        memset(sec->bytes + (start - sec->base), value, sec->pc - start);
    }
}

static int fits(int64_t value, int bytes) {
    switch (bytes) {
        case 1:
            return value >= -128 && value <= 0xFF;
        case 2:
            return value >= -32768 && value <= 0xFFFF;
        default:
            return value >= INT32_MIN && value <= (int64_t)UINT32_MAX;
    }
}

static void report_redefinition(Assembler *as, const char *name, const AsmSymbol *sym) {
    if (sym->line) {
        asm_error(as, "'%s' is already defined at %s:%d", name, sym->line->file, sym->line->number);
    } else {
        asm_error(as, "'%s' is already defined on the command line", name);
    }
}

static void define_label(Assembler *as, const char *text) {
    char name[256];

    if (isa_register_index(text) >= 0) {
        if (as->pass == PASS_DEFINE) {
            asm_error(as, "register name %s cannot be used as a label", text);
        }
        return;
    }
    if (text[0] != '.') {
        snprintf(as->scope, sizeof(as->scope), "%s", text);
    }
    if (!qualify_name(as, text, name, sizeof(name))) {
        if (as->pass == PASS_DEFINE) {
            asm_error(as, "label name too long: %s", text);
        }
        return;
    }

    uint32_t address = current(as)->pc;
    AsmSymbol *sym = symbols_find(&as->symbols, name);

    if (as->pass != PASS_DEFINE) {
        if (sym && sym->value != address) {
            asm_error(as, "label '%s' moved in the final pass (internal error)", name);
            sym->value = address;
            as->changed = 1;
        }
        return;
    }
    if (sym) {
        report_redefinition(as, name, sym);
        return;
    }
    sym = symbols_add(&as->symbols, name);
    if (!sym) {
        asm_error(as, "out of memory");
        return;
    }
    sym->value = address;
    sym->defined = 1;
    sym->kind = as->section == SECTION_TEXT ? SYM_CODE : SYM_DATA;
    sym->line = as->line;
}

static void directive_values(Assembler *as, Parser *p, int width) {
    do {
        Token *t = peek(p);
        if (t->kind == TOK_STRING) {
            if (width != 1) {
                asm_error(as, "strings are only allowed in .byte");
                return;
            }
            emit(as, (const uint8_t *)t->text, (uint32_t)t->length);
            p->pos++;
            continue;
        }

        p->unresolved = 0;
        int64_t value = parse_expression(p);
        if (p->failed) {
            return;
        }
        if (as->pass == PASS_EMIT && !fits(value, width)) {
            asm_error(as, "value %lld does not fit in %d byte%s", (long long)value, width, width > 1 ? "s" : "");
        }

        uint8_t bytes[4];
        for (int i = 0; i < width; i++) {
            bytes[i] = (uint8_t)((uint64_t)value >> (8 * i));
        }
        emit(as, bytes, (uint32_t)width);
    } while (accept(p, ','));

    expect_end(p);
}

static void directive_strings(Assembler *as, Parser *p, int terminate) {
    do {
        Token *t = peek(p);
        if (t->kind != TOK_STRING) {
            asm_error(as, "expected a string");
            return;
        }
        emit(as, (const uint8_t *)t->text, (uint32_t)t->length);
        if (terminate) {
            emit_fill(as, 0, 1);
        }
        p->pos++;
    } while (accept(p, ','));

    expect_end(p);
}

static void directive_space(Assembler *as, Parser *p) {
    int64_t size, fill = 0;

    if (!eval_now(p, &size, "the .space size")) {
        return;
    }
    if (accept(p, ',')) {
        fill = parse_expression(p);
        if (p->failed) {
            return;
        }
    }
    if (size < 0 || size > current(as)->limit - current(as)->pc) {
        asm_error(as, ".space size %lld does not fit the %s section", (long long)size, current(as)->name);
        return;
    }
    if (as->pass == PASS_EMIT && !fits(fill, 1)) {
        asm_error(as, ".space fill value must be a byte");
        return;
    }
    if (expect_end(p)) {
        emit_fill(as, (uint8_t)fill, (uint32_t)size);
    }
}

static void directive_align(Assembler *as, Parser *p) {
    int64_t alignment;

    if (!eval_now(p, &alignment, "the alignment") || !expect_end(p)) {
        return;
    }
    if (alignment < 1 || alignment > 0x1000 || (alignment & (alignment - 1)) != 0) {
        asm_error(as, "alignment must be a power of two up to 4096");
        return;
    }
    uint32_t pc = current(as)->pc;
    emit_fill(as, 0, (uint32_t)((alignment - pc % alignment) % alignment));
}

static void directive_org(Assembler *as, Parser *p) {
    Section *sec = current(as);
    int64_t address;

    if (!eval_now(p, &address, "the .org address") || !expect_end(p)) {
        return;
    }
    if (address < sec->pc || address > sec->limit) {
        asm_error(as, ".org 0x%llX is outside the %s section or before the current address 0x%04X",
                  (long long)address, sec->name, sec->pc);
        return;
    }
    emit_fill(as, 0, (uint32_t)(address - sec->pc));
}

static void directive_equ(Assembler *as, Parser *p) {
    Token *t = peek(p);

    if (t->kind != TOK_IDENT || t->text[0] == '.' || isa_register_index(t->text) >= 0) {
        asm_error(as, "expected a constant name");
        return;
    }
    const char *name = t->text;
    p->pos++;
    if (!accept(p, ',')) {
        asm_error(as, "expected ',' after the constant name");
        return;
    }

    p->unresolved = 0;
    int64_t value = parse_expression(p);
    if (p->failed || !expect_end(p)) {
        return;
    }

    AsmSymbol *sym = symbols_find(&as->symbols, name);
    if (as->pass != PASS_DEFINE) {
        // Constants that use later labels or constants settle during the layout passes
        if (sym && sym->line == as->line && !p->unresolved && (!sym->defined || sym->value != value)) {
            sym->value = value;
            sym->defined = 1;
            as->changed = 1;
        }
        return;
    }
    if (sym) {
        report_redefinition(as, name, sym);
        return;
    }
    sym = symbols_add(&as->symbols, name);
    if (!sym) {
        asm_error(as, "out of memory");
        return;
    }
    sym->kind = SYM_CONST;
    sym->line = as->line;
    sym->value = value;
    sym->defined = !p->unresolved;
}

static void directive_entry(Assembler *as, Parser *p) {
    if (as->pass == PASS_DEFINE && as->entry_line) {
        asm_error(as, "the entry point is already set at %s:%d", as->entry_line->file, as->entry_line->number);
        return;
    }
    as->entry_line = as->line;

    p->unresolved = 0;
    int64_t entry = parse_expression(p);
    if (!p->failed && expect_end(p) && as->pass == PASS_EMIT) {
        as->entry = entry;
    }
}

static void directive(Assembler *as, Parser *p) {
    const char *name = peek(p)->text;
    p->pos++;

    if (name_equals(name, ".text") || name_equals(name, ".data")) {
        if (expect_end(p)) {
            as->section = name_equals(name, ".text") ? SECTION_TEXT : SECTION_DATA;
        }
    } else if (name_equals(name, ".byte")) {
        directive_values(as, p, 1);
    } else if (name_equals(name, ".word")) {
        directive_values(as, p, 2);
    } else if (name_equals(name, ".dword")) {
        directive_values(as, p, 4);
    } else if (name_equals(name, ".ascii")) {
        directive_strings(as, p, 0);
    } else if (name_equals(name, ".asciiz") || name_equals(name, ".string")) {
        directive_strings(as, p, 1);
    } else if (name_equals(name, ".space") || name_equals(name, ".skip")) {
        directive_space(as, p);
    } else if (name_equals(name, ".align")) {
        directive_align(as, p);
    } else if (name_equals(name, ".org")) {
        directive_org(as, p);
    } else if (name_equals(name, ".equ") || name_equals(name, ".set")) {
        directive_equ(as, p);
    } else if (name_equals(name, ".entry")) {
        directive_entry(as, p);
    } else if (name_equals(name, ".error")) {
        Token *message = peek(p);
        if (message->kind != TOK_STRING) {
            asm_error(as, "expected a message in quotes");
            return;
        }
        p->pos++;
        if (expect_end(p) && as->pass == PASS_DEFINE) {
            asm_error(as, "%s", message->text);
        }
    } else if (name_equals(name, ".include")) {
        // The source reader already spliced the file in after this line
        if (peek(p)->kind != TOK_STRING) {
            asm_error(as, "expected a file name in quotes");
        } else {
            p->pos++;
            expect_end(p);
        }
    } else {
        asm_error(as, "unknown directive %s", name);
    }
}

static int parse_register(const Token *t) {
    return t->kind == TOK_IDENT ? isa_register_index(t->text) : -1;
}

static int parse_operand(Parser *p, Operand *op) {
    memset(op, 0, sizeof(*op));

    if (accept(p, '#')) {
        op->mode = IMM_MODE;
    } else if (parse_register(peek(p)) >= 0 &&
               (p->tokens->items[p->pos + 1].kind == TOK_END || token_is_punct(&p->tokens->items[p->pos + 1], ','))) {
        op->mode = REG_MODE;
        op->reg = (uint8_t)parse_register(peek(p));
        p->pos++;
        return 1;
    } else if (accept(p, '[')) {
        int reg = parse_register(peek(p));
        if (reg >= 0) {
            p->pos++;
            op->reg = (uint8_t)reg;
            op->mode = reg == R2_SP ? STK_MODE : reg == R1_BP ? BAS_MODE : REGM_MODE;
            if (!token_is_punct(peek(p), ']')) {
                if (!token_is_punct(peek(p), '+') && !token_is_punct(peek(p), '-')) {
                    asm_error(p->as, "expected '+', '-' or ']' after the register");
                    p->failed = 1;
                    return 0;
                }
                p->unresolved = 0;
                op->value = parse_expression(p);
                op->unresolved = p->unresolved;
                if (op->mode == REGM_MODE) {
                    op->mode = IDX_MODE;
                }
            }
        } else {
            op->mode = MEM_MODE;
            p->unresolved = 0;
            op->value = parse_expression(p);
            op->unresolved = p->unresolved;
        }
        if (!p->failed && !accept(p, ']')) {
            asm_error(p->as, "expected ']'");
            p->failed = 1;
        }
        return !p->failed;
    } else {
        op->mode = IMM_MODE;
    }

    p->unresolved = 0;
    op->value = parse_expression(p);
    op->unresolved = p->unresolved;
    return !p->failed;
}

static const char *mode_description(uint8_t mode) {
    static const char *const names[] = {
        "an immediate", "a register", "a memory address", "a register indirect address",
        "an indexed address", "a stack relative address", "a base relative address",
    };
    return mode < sizeof(names) / sizeof(names[0]) ? names[mode] : "this";
}

static int check_range(Assembler *as, const Operand *op, int64_t min, int64_t max, const char *what) {
    if (as->pass == PASS_EMIT && (op->value < min || op->value > max)) {
        asm_error(as, "%s %lld is out of range (%lld to %lld)", what, (long long)op->value, (long long)min,
                  (long long)max);
        return 0;
    }
    return 1;
}

// Values that fit the immediate field of the first word; an extension word takes any 32 bits
static void operand_limits(const InstructionInfo *info, uint8_t mode, int extended, int64_t *min, int64_t *max) {
    int size = info->format == FMT_REG_REG_SIZE;

    if (extended) {
        *min = size || mode == MEM_MODE ? 0 : INT32_MIN;
        *max = mode == IDX_MODE || mode == STK_MODE || mode == BAS_MODE ? INT32_MAX : (int64_t)UINT32_MAX;
    } else if (size) {
        *min = 0;
        *max = 0x0FFF;
    } else if (mode == MEM_MODE) {
        *min = 0;
        *max = 0xFFFF;
    } else if (mode == IDX_MODE) {
        *min = -2048;
        *max = 2047;
    } else {
        *min = -32768;
        *max = 32767;
    }
}

static int fits_short(const InstructionInfo *info, const Operand *op) {
    int64_t min, max;
    operand_limits(info, op->mode, 0, &min, &max);
    return op->value >= min && op->value <= max;
}

static int set_immediate(Assembler *as, const InstructionInfo *info, const Operand *op, Instruction *in, int extended) {
    static const char *const names[] = {
        [IMM_MODE] = "immediate", [MEM_MODE] = "address", [IDX_MODE] = "index offset",
        [STK_MODE] = "offset", [BAS_MODE] = "offset",
    };
    const char *what = info->format == FMT_REG_REG_SIZE ? "size" : names[op->mode];
    int64_t min, max;

    in->immediate = (uint32_t)op->value;
    in->extended = (uint8_t)extended;
    operand_limits(info, op->mode, 1, &min, &max);
    if (!check_range(as, op, min, max, what)) {
        return 0;
    }
    if (as->pass == PASS_EMIT && !extended && !fits_short(info, op)) {
        asm_error(as, "%s %lld no longer fits after layout (internal error)", what, (long long)op->value);
        return 0;
    }
    return 1;
}

// Stores the variable operand, whose base register goes into reg1 or reg2
static int set_operand(Assembler *as, const InstructionInfo *info, const Operand *op, Instruction *in, int in_reg1,
                       int extended) {
    if (!(info->modes & MODE_BIT(op->mode))) {
        asm_error(as, "%s does not accept %s operand", info->mnemonic, mode_description(op->mode));
        return 0;
    }

    in->mode = op->mode;
    if (op->mode == REG_MODE || op->mode == REGM_MODE || op->mode == IDX_MODE) {
        if (in_reg1) {
            in->reg1 = op->reg;
        } else {
            in->reg2 = op->reg;
        }
    }
    return !(MODE_BIT(op->mode) & MODES_WITH_IMMEDIATE) || set_immediate(as, info, op, in, extended);
}

// The operand stored in the immediate field, if any
static const Operand *immediate_operand(const InstructionInfo *info, const Operand *ops, int count) {
    int index;

    switch (info->format) {
        case FMT_IMM:
        case FMT_OPT_IMM:
        case FMT_OPERAND:
        case FMT_OPERAND_REG:
            index = 0;
            break;
        case FMT_REG_OPERAND:
            index = 1;
            break;
        case FMT_REG_REG_SIZE:
            index = 2;
            break;
        default:
            return NULL;
    }
    return index < count && (MODE_BIT(ops[index].mode) & MODES_WITH_IMMEDIATE) ? &ops[index] : NULL;
}

static int expect_register(Assembler *as, const InstructionInfo *info, const Operand *op, int position) {
    if (op->mode != REG_MODE) {
        asm_error(as, "operand %d of %s must be a register", position, info->mnemonic);
        return 0;
    }
    return 1;
}

static int encode(Assembler *as, const InstructionInfo *info, const Operand *ops, int count, int extended,
                  Instruction *in) {
    static const int operand_counts[] = {
        [FMT_NONE] = 0, [FMT_REG] = 1, [FMT_IMM] = 1, [FMT_OPT_IMM] = 1, [FMT_OPERAND] = 1,
        [FMT_REG_OPERAND] = 2, [FMT_OPERAND_REG] = 2, [FMT_REG_REG] = 2, [FMT_REG_REG_SIZE] = 3,
    };
    int expected = operand_counts[info->format];

    if (count != expected && !(info->format == FMT_OPT_IMM && count == 0)) {
        asm_error(as, "%s takes %d operand%s", info->mnemonic, expected, expected == 1 ? "" : "s");
        return 0;
    }

    memset(in, 0, sizeof(*in));
    in->opcode = info->opcode;

    switch (info->format) {
        case FMT_NONE:
            return 1;
        case FMT_REG:
            in->mode = REG_MODE;
            in->reg1 = ops[0].reg;
            return expect_register(as, info, &ops[0], 1);
        case FMT_IMM:
        case FMT_OPT_IMM:
            if (count == 0) {
                return 1;
            }
            if (ops[0].mode != IMM_MODE) {
                asm_error(as, "%s takes an immediate operand", info->mnemonic);
                return 0;
            }
            if (info->opcode == INT_OP && !check_range(as, &ops[0], 0, 255, "interrupt vector")) {
                return 0;
            }
            return set_operand(as, info, &ops[0], in, 1, extended);
        case FMT_OPERAND:
            return set_operand(as, info, &ops[0], in, 1, extended);
        case FMT_REG_OPERAND:
            in->reg1 = ops[0].reg;
            return expect_register(as, info, &ops[0], 1) && set_operand(as, info, &ops[1], in, 0, extended);
        case FMT_OPERAND_REG:
            in->reg1 = ops[1].reg;
            return expect_register(as, info, &ops[1], 2) && set_operand(as, info, &ops[0], in, 0, extended);
        case FMT_REG_REG:
            in->mode = REG_MODE;
            in->reg1 = ops[0].reg;
            in->reg2 = ops[1].reg;
            return expect_register(as, info, &ops[0], 1) && expect_register(as, info, &ops[1], 2);
        case FMT_REG_REG_SIZE:
            if (!expect_register(as, info, &ops[0], 1) || !expect_register(as, info, &ops[1], 2)) {
                return 0;
            }
            in->reg1 = ops[0].reg;
            in->reg2 = ops[1].reg;
            if (ops[2].mode == REG_MODE) {
                in->mode = REG_MODE;
                in->immediate = ops[2].reg;
                return 1;
            }
            if (ops[2].mode != IMM_MODE) {
                asm_error(as, "the size of %s must be an immediate or a register", info->mnemonic);
                return 0;
            }
            in->mode = IMM_MODE;
            return set_immediate(as, info, &ops[2], in, extended);
        default:
            return 0;
    }
}

static void emit_instruction(Assembler *as, const Instruction *in) {
    uint32_t words[2];
    int count = isa_encode(in, words);
    for (int w = 0; w < count; w++) {
        uint8_t bytes[4];
        for (int i = 0; i < 4; i++) {
            bytes[i] = (uint8_t)(words[w] >> (8 * i));
        }
        emit(as, bytes, 4);
    }
}

// Alternative names for conditional jumps
static const InstructionInfo *find_instruction(const char *mnemonic) {
    static const char *const aliases[][2] = {
        { "JE", "JZ" }, { "JNE", "JNZ" }, { "JB", "JC" }, { "JNAE", "JC" }, { "JNB", "JAE" },
        { "JNC", "JAE" }, { "JNA", "JBE" }, { "JNBE", "JA" }, { "JNGE", "JL" }, { "JNL", "JGE" },
        { "JNG", "JLE" }, { "JNLE", "JG" },
    };
    for (size_t i = 0; i < sizeof(aliases) / sizeof(aliases[0]); i++) {
        if (name_equals(mnemonic, aliases[i][0])) {
            return isa_by_mnemonic(aliases[i][1]);
        }
    }
    return isa_by_mnemonic(mnemonic);
}

static void instruction(Assembler *as, Parser *p, LineResult *result) {
    Token *t = peek(p);
    const InstructionInfo *info = find_instruction(t->text);

    if (!info) {
        asm_error(as, "unknown instruction '%s'", t->text);
        return;
    }
    p->pos++;

    if (as->section != SECTION_TEXT) {
        asm_error(as, "instructions must be in the .text section");
        return;
    }
    if (current(as)->pc % 4 != 0) {
        asm_error(as, "instruction at unaligned address 0x%04X, use .align 4", current(as)->pc);
        return;
    }

    Operand ops[3];
    int count = 0;
    if (peek(p)->kind != TOK_END) {
        do {
            if (count == 3) {
                asm_error(as, "too many operands");
                return;
            }
            if (!parse_operand(p, &ops[count++])) {
                return;
            }
        } while (accept(p, ','));
    }
    if (!expect_end(p)) {
        return;
    }

    result->is_code = 1;

    // Forward references start in the short form and widen once their value is known
    const Operand *immediate = immediate_operand(info, ops, count);
    if (as->pass != PASS_EMIT && immediate && !immediate->unresolved && !result->extended &&
        !fits_short(info, immediate)) {
        result->extended = 1;
        as->changed = 1;
    }

    Instruction in;
    if (encode(as, info, ops, count, result->extended, &in)) {
        emit_instruction(as, &in);
    }
}

static int conditions_active(const Assembler *as) {
    return as->condition_depth == 0 || as->conditions[as->condition_depth - 1].active;
}

// Handles .if, .ifdef, .ifndef, .else and .endif; returns 0 for any other statement
static int conditional(Assembler *as, Parser *p, LineResult *result) {
    const Token *t = &p->tokens->items[p->pos];
    const char *name = t->kind == TOK_IDENT ? t->text : "";
    int is_if = name_equals(name, ".if"), is_ifdef = name_equals(name, ".ifdef"), is_ifndef = name_equals(name, ".ifndef");

    if (is_if || is_ifdef || is_ifndef) {
        int parent_active = conditions_active(as);
        p->pos++;

        if (as->condition_depth == ASM_MAX_CONDITIONS) {
            asm_error(as, "conditionals are nested too deeply");
            return 1;
        }
        if (parent_active && as->pass != PASS_DEFINE && is_if) {
            int64_t value;
            eval_now(p, &value, "a .if condition");
        } else if (parent_active && as->pass == PASS_DEFINE) {
            int64_t value = 0;
            if (is_if) {
                if (!eval_now(p, &value, "a .if condition")) {
                    value = 0;
                }
            } else if (peek(p)->kind != TOK_IDENT) {
                asm_error(as, "expected a symbol name");
            } else {
                char qualified[256];
                AsmSymbol *sym = qualify_name(as, peek(p)->text, qualified, sizeof(qualified))
                                     ? symbols_find(&as->symbols, qualified) : NULL;
                value = (sym != NULL) == is_ifdef;
                p->pos++;
            }
            expect_end(p);
            result->condition = value != 0;
        }

        int active = parent_active && result->condition;
        as->conditions[as->condition_depth++] = (Condition){ active, active, parent_active };
        return 1;
    }

    if (name_equals(name, ".else") || name_equals(name, ".endif")) {
        p->pos++;
        if (as->condition_depth == 0) {
            asm_error(as, "%s without .if", name);
            return 1;
        }
        Condition *c = &as->conditions[as->condition_depth - 1];
        if (name_equals(name, ".endif")) {
            as->condition_depth--;
        } else {
            c->active = c->parent_active && !c->taken;
            c->taken = 1;
        }
        expect_end(p);
        return 1;
    }
    return 0;
}

static void assemble_line(Assembler *as, size_t index, int labels_only) {
    LineResult *result = &as->results[index];
    TokenList tokens;
    char error[128];

    if (!lex_line(as->line->text, &tokens, error, sizeof(error))) {
        asm_error(as, "%s", error);
        return;
    }

    Parser p = { as, &tokens, 0, 0, 0 };
    int active = conditions_active(as);
    while (tokens.items[p.pos].kind == TOK_IDENT && token_is_punct(&tokens.items[p.pos + 1], ':')) {
        if (active) {
            define_label(as, tokens.items[p.pos].text);
        }
        p.pos += 2;
    }

    if (conditional(as, &p, result) || !active) {
        tokens_free(&tokens);
        return;
    }

    int section = as->section;
    uint32_t start = current(as)->pc;
    as->statement_address = start;
    result->section = -1;

    Token *t = &tokens.items[p.pos];
    if (labels_only) {
        // A macro invocation; its expansion follows as separate lines
    } else if (t->kind == TOK_IDENT && t->text[0] == '.') {
        directive(as, &p);
    } else if (t->kind == TOK_IDENT) {
        instruction(as, &p, result);
    } else if (t->kind != TOK_END) {
        asm_error(as, "expected an instruction or directive");
    }

    if (as->section == section && as->sections[section].pc > start) {
        result->section = section;
        result->address = start;
        result->size = as->sections[section].pc - start;
    }
    tokens_free(&tokens);
}

static void run_pass(Assembler *as, int pass) {
    as->pass = pass;
    as->changed = 0;
    for (int i = 0; i < SECTION_COUNT; i++) {
        as->sections[i].pc = as->sections[i].base;
        as->sections[i].end = as->sections[i].base;
    }
    as->section = SECTION_TEXT;
    as->section_depth = 0;
    as->condition_depth = 0;
    as->scope[0] = '\0';

    for (size_t i = 0; i < as->line_count && as->errors < ASM_MAX_ERRORS; i++) {
        as->line = &as->lines[i];
        switch (as->line->kind) {
            case LINE_INCLUDE_BEGIN:
                as->section_stack[as->section_depth++] = as->section;
                break;
            case LINE_INCLUDE_END:
                // An included file does not change the section of the file that includes it
                as->section = as->section_stack[--as->section_depth];
                break;
            case LINE_MACRO_DEFINITION:
                break;
            default:
                assemble_line(as, i, as->line->kind == LINE_MACRO_CALL);
                break;
        }
    }
    as->line = NULL;

    if (as->condition_depth > 0 && pass == PASS_DEFINE) {
        asm_error(as, "missing .endif");
    }
}

// Defines NAME=VALUE constants given on the command line before any source is assembled
static void define_command_line_constants(Assembler *as) {
    for (int i = 0; i < as->define_count; i++) {
        char name[128];
        const char *equals = strchr(as->defines[i], '=');
        size_t length = equals ? (size_t)(equals - as->defines[i]) : strlen(as->defines[i]);
        int64_t value = 1;

        snprintf(name, sizeof(name), "%.*s", (int)length, as->defines[i]);
        if (equals) {
            TokenList tokens;
            char error[64];
            if (!lex_line(equals + 1, &tokens, error, sizeof(error))) {
                asm_error(as, "invalid value for -D %s", name);
                continue;
            }
            Parser p = { as, &tokens, 0, 0, 0 };
            as->pass = PASS_DEFINE;
            value = parse_expression(&p);
            if (!p.failed && (p.unresolved || tokens.items[p.pos].kind != TOK_END)) {
                asm_error(as, "invalid value for -D %s", name);
            }
            tokens_free(&tokens);
        }

        if (length == 0 || isa_register_index(name) >= 0 || symbols_find(&as->symbols, name)) {
            asm_error(as, "cannot define '%s' on the command line", name);
            continue;
        }
        AsmSymbol *sym = symbols_add(&as->symbols, name);
        if (!sym) {
            asm_error(as, "out of memory");
            return;
        }
        sym->kind = SYM_CONST;
        sym->value = value;
        sym->defined = 1;
    }
}

// Repeats layout passes until no label moves; each pass can only widen instructions, so this ends
static void settle_layout(Assembler *as) {
    enum { MAX_LAYOUT_PASSES = 1000 };

    for (int pass = 0; pass < MAX_LAYOUT_PASSES; pass++) {
        run_pass(as, PASS_LAYOUT);
        if (!as->changed) {
            break;
        }
    }
    as->pass = PASS_DEFINE;
    if (as->changed) {
        asm_error(as, "the program layout does not settle");
    }
    for (size_t i = 0; i < as->symbols.count; i++) {
        const AsmSymbol *sym = &as->symbols.items[i];
        if (!sym->defined && sym->kind == SYM_CONST) {
            as->line = sym->line;
            asm_error(as, "cannot resolve the value of '%s'", sym->name);
        }
    }
    as->line = NULL;
}

int asm_assemble(Assembler *as, const char *path) {
    define_command_line_constants(as);
    if (as->errors || !source_load(as, path)) {
        return 0;
    }

    as->results = calloc(as->line_count ? as->line_count : 1, sizeof(LineResult));
    for (size_t i = 0; as->results && i < as->line_count; i++) {
        as->results[i].section = -1;
    }
    for (int i = 0; i < SECTION_COUNT; i++) {
        as->sections[i].bytes = calloc(as->sections[i].limit - as->sections[i].base, 1);
        if (!as->sections[i].bytes || !as->results) {
            asm_error(as, "out of memory");
            return 0;
        }
    }

    run_pass(as, PASS_DEFINE);
    if (as->errors == 0) {
        settle_layout(as);
    }
    if (as->errors == 0) {
        as->entry = as->sections[SECTION_TEXT].base;
        run_pass(as, PASS_EMIT);
    }

    const Section *text = &as->sections[SECTION_TEXT];
    if (as->errors == 0 && as->entry_line) {
        as->line = as->entry_line;
        if (as->entry < text->base || as->entry >= text->end) {
            asm_error(as, "entry point 0x%llX is outside the assembled code", (long long)as->entry);
        } else if (as->entry % 4 != 0) {
            asm_error(as, "entry point 0x%llX is not instruction aligned", (long long)as->entry);
        }
        as->line = NULL;
    }
    return as->errors == 0;
}
