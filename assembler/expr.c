#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "asm.h"

// Local labels (".name") are scoped to the most recent global label
int qualify_name(Assembler *as, const char *name, char *out, size_t size) {
    int n = name[0] == '.' ? snprintf(out, size, "%s%s", as->scope, name) : snprintf(out, size, "%s", name);
    return n > 0 && (size_t)n < size;
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

static int64_t fail(Parser *p, const char *format, ...) {
    if (!p->failed) {
        char message[256];
        va_list args;
        va_start(args, format);
        vsnprintf(message, sizeof(message), format, args);
        va_end(args);
        asm_error(p->as, "%s", message);
    }
    p->failed = 1;
    return 0;
}

static int64_t parse_logical_or(Parser *p);

static int64_t parse_primary(Parser *p) {
    Token *t = peek(p);

    if (t->kind == TOK_NUMBER) {
        p->pos++;
        return t->number;
    }
    if (t->kind == TOK_IDENT) {
        char name[256];
        p->pos++;
        if (isa_register_index(t->text) >= 0) {
            return fail(p, "register %s cannot be used in an expression", t->text);
        }
        if (!qualify_name(p->as, t->text, name, sizeof(name))) {
            return fail(p, "symbol name too long: %s", t->text);
        }
        AsmSymbol *sym = symbols_find(&p->as->symbols, name);
        if (sym && sym->defined) {
            return sym->value;
        }
        if (p->as->pass != PASS_EMIT) {
            p->unresolved = 1;
            return 0;
        }
        return fail(p, "undefined symbol '%s'", name);
    }
    if (accept(p, '$')) {
        return p->as->statement_address;
    }
    if (accept(p, '(')) {
        int64_t value = parse_logical_or(p);
        if (!accept(p, ')')) {
            return fail(p, "expected ')'");
        }
        return value;
    }
    return fail(p, "expected an expression");
}

static int64_t parse_unary(Parser *p) {
    if (accept(p, '-')) {
        return (int64_t)(0 - (uint64_t)parse_unary(p));
    }
    if (accept(p, '+')) {
        return parse_unary(p);
    }
    if (accept(p, '~')) {
        return ~parse_unary(p);
    }
    if (accept(p, '!')) {
        return !parse_unary(p);
    }
    return parse_primary(p);
}

static int64_t parse_mul(Parser *p) {
    int64_t value = parse_unary(p);
    for (;;) {
        if (accept(p, '*')) {
            value = (int64_t)((uint64_t)value * (uint64_t)parse_unary(p));
        } else if (token_is_punct(peek(p), '/') || token_is_punct(peek(p), '%')) {
            int op = peek(p)->punct;
            p->pos++;
            int64_t divisor = parse_unary(p);
            if (divisor == 0) {
                if (!p->unresolved) {
                    fail(p, "division by zero");
                }
                value = 0;
            } else if (divisor == -1) {
                value = op == '/' ? (int64_t)(0 - (uint64_t)value) : 0;
            } else {
                value = op == '/' ? value / divisor : value % divisor;
            }
        } else {
            return value;
        }
    }
}

static int64_t parse_add(Parser *p) {
    int64_t value = parse_mul(p);
    for (;;) {
        if (accept(p, '+')) {
            value = (int64_t)((uint64_t)value + (uint64_t)parse_mul(p));
        } else if (accept(p, '-')) {
            value = (int64_t)((uint64_t)value - (uint64_t)parse_mul(p));
        } else {
            return value;
        }
    }
}

static int64_t parse_shift(Parser *p) {
    int64_t value = parse_add(p);
    for (;;) {
        int left = accept(p, OP_SHL);
        if (!left && !accept(p, OP_SHR)) {
            return value;
        }
        int64_t amount = parse_add(p);
        if (amount < 0 || amount > 63) {
            if (!p->unresolved) {
                fail(p, "shift amount out of range");
            }
            amount = 0;
        }
        if (left) {
            value = (int64_t)((uint64_t)value << amount);
        } else {
            value = value < 0 ? ~(~value >> amount) : value >> amount;
        }
    }
}

static int64_t parse_relational(Parser *p) {
    int64_t value = parse_shift(p);
    for (;;) {
        if (accept(p, '<')) {
            value = value < parse_shift(p);
        } else if (accept(p, '>')) {
            value = value > parse_shift(p);
        } else if (accept(p, OP_LE)) {
            value = value <= parse_shift(p);
        } else if (accept(p, OP_GE)) {
            value = value >= parse_shift(p);
        } else {
            return value;
        }
    }
}

static int64_t parse_equality(Parser *p) {
    int64_t value = parse_relational(p);
    for (;;) {
        if (accept(p, OP_EQ)) {
            value = value == parse_relational(p);
        } else if (accept(p, OP_NE)) {
            value = value != parse_relational(p);
        } else {
            return value;
        }
    }
}

static int64_t parse_and(Parser *p) {
    int64_t value = parse_equality(p);
    while (accept(p, '&')) {
        value &= parse_equality(p);
    }
    return value;
}

static int64_t parse_xor(Parser *p) {
    int64_t value = parse_and(p);
    while (accept(p, '^')) {
        value ^= parse_and(p);
    }
    return value;
}

static int64_t parse_or(Parser *p) {
    int64_t value = parse_xor(p);
    while (accept(p, '|')) {
        value |= parse_xor(p);
    }
    return value;
}

static int64_t parse_logical_and(Parser *p) {
    int64_t value = parse_or(p);
    while (accept(p, OP_AND)) {
        int64_t right = parse_or(p);
        value = value && right;
    }
    return value;
}

static int64_t parse_logical_or(Parser *p) {
    int64_t value = parse_logical_and(p);
    while (accept(p, OP_OR)) {
        int64_t right = parse_logical_and(p);
        value = value || right;
    }
    return value;
}

int64_t parse_expression(Parser *p) {
    return parse_logical_or(p);
}
