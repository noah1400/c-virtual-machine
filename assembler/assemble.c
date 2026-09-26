#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asm.h"
#include "vm_types.h"

// A .equ whose value depends on labels defined later in the source
struct PendingConstant {
    size_t line;
    char scope[128];
};

typedef struct {
    uint8_t mode;
    uint8_t reg;
    int64_t value;
    int unresolved;
} Operand;

void asm_error(Assembler *as, const char *format, ...) {
    va_list args;

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
    free(as->lines);
    free(as->files);
    free(as->results);
    free(as->pending);
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

// Evaluates an expression whose value is needed during pass 1 to lay out the program
static int eval_now(Parser *p, int64_t *value, const char *what) {
    p->unresolved = 0;
    *value = parse_expression(p);
    if (p->failed) {
        return 0;
    }
    if (p->unresolved) {
        asm_error(p->as, "%s must not depend on symbols defined later", what);
        p->failed = 1;
        return 0;
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
    if (as->pass == 2 && bytes) {
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
    if (as->pass == 2 && sec->pc > start) {
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

static void define_label(Assembler *as, const char *text) {
    char name[256];

    if (isa_register_index(text) >= 0) {
        if (as->pass == 1) {
            asm_error(as, "register name %s cannot be used as a label", text);
        }
        return;
    }
    if (text[0] != '.') {
        snprintf(as->scope, sizeof(as->scope), "%s", text);
    }
    if (!qualify_name(as, text, name, sizeof(name))) {
        if (as->pass == 1) {
            asm_error(as, "label name too long: %s", text);
        }
        return;
    }

    uint32_t address = current(as)->pc;
    AsmSymbol *sym = symbols_find(&as->symbols, name);

    if (as->pass == 2) {
        if (sym && sym->value != address) {
            asm_error(as, "label '%s' moved between passes (internal error)", name);
        }
        return;
    }
    if (sym) {
        asm_error(as, "'%s' is already defined at %s:%d", name, sym->line->file, sym->line->number);
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
        if (as->pass == 2 && !fits(value, width)) {
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
    if (accept(p, ',') && !eval_now(p, &fill, "the .space fill value")) {
        return;
    }
    if (size < 0 || size > current(as)->limit - current(as)->pc) {
        asm_error(as, ".space size %lld does not fit the %s section", (long long)size, current(as)->name);
        return;
    }
    if (!fits(fill, 1)) {
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
    if (p->failed || !expect_end(p) || as->pass == 2) {
        return;
    }

    AsmSymbol *sym = symbols_find(&as->symbols, name);
    if (sym) {
        asm_error(as, "'%s' is already defined at %s:%d", name, sym->line->file, sym->line->number);
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

    if (p->unresolved) {
        struct PendingConstant *pending = realloc(as->pending, (as->pending_count + 1) * sizeof(*pending));
        if (!pending) {
            asm_error(as, "out of memory");
            return;
        }
        as->pending = pending;
        pending[as->pending_count].line = (size_t)(as->line - as->lines);
        snprintf(pending[as->pending_count].scope, sizeof(pending->scope), "%s", as->scope);
        as->pending_count++;
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
    if (as->pass == 2 && (op->value < min || op->value > max)) {
        asm_error(as, "%s %lld is out of range (%lld to %lld)", what, (long long)op->value, (long long)min,
                  (long long)max);
        return 0;
    }
    return 1;
}

// Stores the variable operand, whose base register goes into reg1 or reg2
static int set_operand(Assembler *as, const InstructionInfo *info, const Operand *op, Instruction *in, int in_reg1) {
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

    switch (op->mode) {
        case IMM_MODE:
            in->immediate = (uint16_t)op->value;
            return check_range(as, op, 0, 0xFFFF, "immediate");
        case MEM_MODE:
            in->immediate = (uint16_t)op->value;
            return check_range(as, op, 0, 0xFFFF, "address");
        case IDX_MODE:
            in->immediate = (uint16_t)(op->value & 0x0FFF);
            return check_range(as, op, -2048, 2047, "index offset");
        case STK_MODE:
        case BAS_MODE:
            in->immediate = (uint16_t)op->value;
            return check_range(as, op, -32768, 32767, "offset");
        default:
            return 1;
    }
}

static int expect_register(Assembler *as, const InstructionInfo *info, const Operand *op, int position) {
    if (op->mode != REG_MODE) {
        asm_error(as, "operand %d of %s must be a register", position, info->mnemonic);
        return 0;
    }
    return 1;
}

static int encode(Assembler *as, const InstructionInfo *info, const Operand *ops, int count, Instruction *in) {
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
            return set_operand(as, info, &ops[0], in, 1);
        case FMT_OPERAND:
            return set_operand(as, info, &ops[0], in, 1);
        case FMT_REG_OPERAND:
            in->reg1 = ops[0].reg;
            return expect_register(as, info, &ops[0], 1) && set_operand(as, info, &ops[1], in, 0);
        case FMT_OPERAND_REG:
            in->reg1 = ops[1].reg;
            return expect_register(as, info, &ops[1], 2) && set_operand(as, info, &ops[0], in, 0);
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
            in->immediate = (uint16_t)((ops[1].reg << 12) | (ops[2].value & 0x0FFF));
            return check_range(as, &ops[2], 0, 0x0FFF, "size");
        default:
            return 0;
    }
}

static void instruction(Assembler *as, Parser *p, LineResult *result) {
    Token *t = peek(p);
    const InstructionInfo *info = isa_by_mnemonic(t->text);

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

    Instruction in;
    if (!encode(as, info, ops, count, &in)) {
        return;
    }

    uint8_t bytes[4];
    uint32_t word = isa_encode(&in);
    for (int i = 0; i < 4; i++) {
        bytes[i] = (uint8_t)(word >> (8 * i));
    }
    emit(as, bytes, 4);
    result->is_code = 1;
}

static void assemble_line(Assembler *as, size_t index) {
    LineResult *result = &as->results[index];
    TokenList tokens;
    char error[128];

    if (!lex_line(as->line->text, &tokens, error, sizeof(error))) {
        asm_error(as, "%s", error);
        return;
    }

    Parser p = { as, &tokens, 0, 0, 0 };
    while (tokens.items[p.pos].kind == TOK_IDENT && token_is_punct(&tokens.items[p.pos + 1], ':')) {
        define_label(as, tokens.items[p.pos].text);
        p.pos += 2;
    }

    int section = as->section;
    uint32_t start = current(as)->pc;
    as->statement_address = start;
    result->section = -1;

    Token *t = &tokens.items[p.pos];
    if (t->kind == TOK_IDENT && t->text[0] == '.') {
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
    for (int i = 0; i < SECTION_COUNT; i++) {
        as->sections[i].pc = as->sections[i].base;
        as->sections[i].end = as->sections[i].base;
    }
    as->section = SECTION_TEXT;
    as->section_depth = 0;
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
            default:
                assemble_line(as, i);
                break;
        }
    }
    as->line = NULL;
}

// Constants that referred to later labels are evaluated once every label is known
static void resolve_pending(Assembler *as) {
    int progress = 1;

    while (progress) {
        progress = 0;
        for (size_t i = 0; i < as->pending_count; i++) {
            const SourceLine *line = &as->lines[as->pending[i].line];
            TokenList tokens;
            char error[128];

            if (!lex_line(line->text, &tokens, error, sizeof(error))) {
                continue;
            }
            int pos = 0;
            while (tokens.items[pos].kind == TOK_IDENT && token_is_punct(&tokens.items[pos + 1], ':')) {
                pos += 2;
            }
            AsmSymbol *sym = symbols_find(&as->symbols, tokens.items[pos + 1].text);
            if (sym && !sym->defined) {
                Parser p = { as, &tokens, pos + 3, 0, 0 };
                snprintf(as->scope, sizeof(as->scope), "%s", as->pending[i].scope);
                as->line = line;
                as->statement_address = as->results[as->pending[i].line].address;
                int64_t value = parse_expression(&p);
                if (!p.unresolved && !p.failed) {
                    sym = symbols_find(&as->symbols, tokens.items[pos + 1].text);
                    sym->value = value;
                    sym->defined = 1;
                    progress = 1;
                }
            }
            tokens_free(&tokens);
        }
    }

    for (size_t i = 0; i < as->pending_count; i++) {
        const SourceLine *line = &as->lines[as->pending[i].line];
        for (size_t s = 0; s < as->symbols.count; s++) {
            AsmSymbol *sym = &as->symbols.items[s];
            if (sym->line == line && !sym->defined) {
                as->line = line;
                asm_error(as, "cannot resolve the value of '%s'", sym->name);
            }
        }
    }
    as->line = NULL;
}

int asm_assemble(Assembler *as, const char *path) {
    if (!source_load(as, path)) {
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

    run_pass(as, 1);
    if (as->errors == 0) {
        as->pass = 1;
        resolve_pending(as);
    }
    if (as->errors == 0) {
        run_pass(as, 2);
    }
    return as->errors == 0;
}
