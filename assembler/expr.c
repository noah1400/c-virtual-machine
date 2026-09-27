#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "asm.h"

// A value and how many times the address of its base is added to it. In an object file a label is
// its offset plus once its section, and only a weight of 0 or 1 can be stored.
typedef struct {
    int64_t value;
    int base;
    int weight;
} Value;

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

static Value fail(Parser *p, const char *format, ...) {
    if (!p->failed && !p->quiet) {
        char message[256];
        va_list args;
        va_start(args, format);
        vsnprintf(message, sizeof(message), format, args);
        va_end(args);
        asm_error(p->as, "%s", message);
    }
    p->failed = 1;
    return (Value){ 0, BASE_NONE, 0 };
}

static Value plain(int64_t value) {
    return (Value){ value, BASE_NONE, 0 };
}

// Operators other than + and - only take plain numbers
static int plain_operands(Parser *p, Value a, Value b, const char *op) {
    if (a.weight || b.weight) {
        fail(p, "an address in an object file cannot be used with '%s'", op);
        return 0;
    }
    return 1;
}

static Value combine(Parser *p, Value a, Value b, int sign) {
    uint64_t sum = sign > 0 ? (uint64_t)a.value + (uint64_t)b.value : (uint64_t)a.value - (uint64_t)b.value;
    Value result = { (int64_t)sum, a.base, a.weight };
    if (b.weight) {
        if (result.weight && result.base != b.base) {
            return fail(p, "addresses from different sections or symbols cannot be combined");
        }
        result.base = b.base;
        result.weight += sign * b.weight;
    }
    if (result.weight == 0) {
        result.base = BASE_NONE;
    }
    return result;
}

static Value parse_logical_or(Parser *p);

static Value symbol_value(Parser *p, const char *name) {
    AsmSymbol *sym = p->constants ? symbols_find(p->constants, name) : NULL;
    if (!sym) {
        sym = symbols_find(&p->as->symbols, name);
    }
    if (sym && sym->external) {
        return (Value){ 0, BASE_SYMBOL + (int)(sym - p->as->symbols.items), 1 };
    }
    if (sym && sym->defined) {
        return (Value){ sym->value, sym->base, sym->base != BASE_NONE };
    }
    if (p->as->pass != PASS_EMIT) {
        p->unresolved = 1;
        return plain(0);
    }
    return fail(p, "undefined symbol '%s'", name);
}

static Value parse_primary(Parser *p) {
    Token *t = peek(p);

    if (t->kind == TOK_NUMBER) {
        p->pos++;
        p->floats += t->is_float;
        return plain(t->number);
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
        return symbol_value(p, name);
    }
    if (accept(p, '$')) {
        Assembler *as = p->as;
        p->unresolved |= p->constants != NULL;
        if (as->object) {
            return (Value){ as->statement_address, as->section == SECTION_TEXT ? BASE_TEXT : BASE_DATA, 1 };
        }
        return plain(as->statement_address);
    }
    if (accept(p, '(')) {
        Value value = parse_logical_or(p);
        if (!accept(p, ')')) {
            return fail(p, "expected ')'");
        }
        return value;
    }
    return fail(p, "expected an expression");
}

static Value parse_unary(Parser *p) {
    if (accept(p, '-')) {
        int floats = p->floats;
        Value value = parse_unary(p);
        // Negating a float flips its sign bit
        value.value = p->floats > floats ? value.value ^ 0x80000000 : (int64_t)(0 - (uint64_t)value.value);
        value.weight = -value.weight;
        return value;
    }
    if (accept(p, '+')) {
        return parse_unary(p);
    }
    if (accept(p, '~')) {
        p->operators++;
        Value value = parse_unary(p);
        return plain_operands(p, value, plain(0), "~") ? plain(~value.value) : plain(0);
    }
    if (accept(p, '!')) {
        p->operators++;
        Value value = parse_unary(p);
        return plain_operands(p, value, plain(0), "!") ? plain(!value.value) : plain(0);
    }
    return parse_primary(p);
}

static Value parse_mul(Parser *p) {
    Value value = parse_unary(p);
    for (;;) {
        if (accept(p, '*')) {
            p->operators++;
            Value right = parse_unary(p);
            if (plain_operands(p, value, right, "*")) {
                value = plain((int64_t)((uint64_t)value.value * (uint64_t)right.value));
            }
        } else if (token_is_punct(peek(p), '/') || token_is_punct(peek(p), '%')) {
            int op = peek(p)->punct;
            p->pos++;
            p->operators++;
            Value divisor = parse_unary(p);
            if (!plain_operands(p, value, divisor, op == '/' ? "/" : "%")) {
                continue;
            }
            if (divisor.value == 0) {
                if (!p->unresolved) {
                    fail(p, "division by zero");
                }
                value = plain(0);
            } else if (divisor.value == -1) {
                value = plain(op == '/' ? (int64_t)(0 - (uint64_t)value.value) : 0);
            } else {
                value = plain(op == '/' ? value.value / divisor.value : value.value % divisor.value);
            }
        } else {
            return value;
        }
    }
}

static Value parse_add(Parser *p) {
    Value value = parse_mul(p);
    for (;;) {
        if (accept(p, '+')) {
            p->operators++;
            value = combine(p, value, parse_mul(p), 1);
        } else if (accept(p, '-')) {
            p->operators++;
            value = combine(p, value, parse_mul(p), -1);
        } else {
            return value;
        }
    }
}

static Value parse_shift(Parser *p) {
    Value value = parse_add(p);
    for (;;) {
        int left = accept(p, OP_SHL);
        if (!left && !accept(p, OP_SHR)) {
            return value;
        }
        p->operators++;
        Value amount = parse_add(p);
        if (!plain_operands(p, value, amount, left ? "<<" : ">>")) {
            continue;
        }
        if (amount.value < 0 || amount.value > 63) {
            if (!p->unresolved) {
                fail(p, "shift amount out of range");
            }
            amount.value = 0;
        }
        if (left) {
            value.value = (int64_t)((uint64_t)value.value << amount.value);
        } else {
            value.value = value.value < 0 ? ~(~value.value >> amount.value) : value.value >> amount.value;
        }
    }
}

// Applies a comparison or bitwise operator to plain numbers
static Value apply(Parser *p, Value a, Value b, int op, const char *name) {
    if (!plain_operands(p, a, b, name)) {
        return plain(0);
    }
    switch (op) {
        case '<':
            return plain(a.value < b.value);
        case '>':
            return plain(a.value > b.value);
        case OP_LE:
            return plain(a.value <= b.value);
        case OP_GE:
            return plain(a.value >= b.value);
        case OP_EQ:
            return plain(a.value == b.value);
        case OP_NE:
            return plain(a.value != b.value);
        case '&':
            return plain(a.value & b.value);
        case '^':
            return plain(a.value ^ b.value);
        case '|':
            return plain(a.value | b.value);
        case OP_AND:
            return plain(a.value && b.value);
        default:
            return plain(a.value || b.value);
    }
}

static Value parse_relational(Parser *p) {
    static const struct {
        int op;
        const char *name;
    } operators[] = { { '<', "<" }, { '>', ">" }, { OP_LE, "<=" }, { OP_GE, ">=" } };
    Value value = parse_shift(p);
    for (;;) {
        size_t i = 0;
        while (i < sizeof(operators) / sizeof(operators[0]) && !accept(p, operators[i].op)) {
            i++;
        }
        if (i == sizeof(operators) / sizeof(operators[0])) {
            return value;
        }
        p->operators++;
        value = apply(p, value, parse_shift(p), operators[i].op, operators[i].name);
    }
}

static Value parse_equality(Parser *p) {
    Value value = parse_relational(p);
    for (;;) {
        int equal = accept(p, OP_EQ);
        if (!equal && !accept(p, OP_NE)) {
            return value;
        }
        p->operators++;
        value = apply(p, value, parse_relational(p), equal ? OP_EQ : OP_NE, equal ? "==" : "!=");
    }
}

static Value parse_and(Parser *p) {
    Value value = parse_equality(p);
    while (accept(p, '&')) {
        p->operators++;
        value = apply(p, value, parse_equality(p), '&', "&");
    }
    return value;
}

static Value parse_xor(Parser *p) {
    Value value = parse_and(p);
    while (accept(p, '^')) {
        p->operators++;
        value = apply(p, value, parse_and(p), '^', "^");
    }
    return value;
}

static Value parse_or(Parser *p) {
    Value value = parse_xor(p);
    while (accept(p, '|')) {
        p->operators++;
        value = apply(p, value, parse_xor(p), '|', "|");
    }
    return value;
}

static Value parse_logical_and(Parser *p) {
    Value value = parse_or(p);
    while (accept(p, OP_AND)) {
        p->operators++;
        value = apply(p, value, parse_or(p), OP_AND, "&&");
    }
    return value;
}

static Value parse_logical_or(Parser *p) {
    Value value = parse_logical_and(p);
    while (accept(p, OP_OR)) {
        p->operators++;
        value = apply(p, value, parse_logical_and(p), OP_OR, "||");
    }
    return value;
}

// Float literals may only stand alone, with an optional sign, since expressions use integer arithmetic.
// The base of the value is left in p->base; only callers that set p->relocatable take an address
// of an object file.
int64_t parse_expression(Parser *p) {
    int floats = p->floats, operators = p->operators;
    Value value = parse_logical_or(p);

    p->base = BASE_NONE;
    if (p->floats > floats && (p->floats - floats > 1 || p->operators > operators)) {
        return fail(p, "floating-point constants cannot be combined with operators").value;
    }
    if (value.weight != 0 && !p->failed) {
        if (value.weight != 1) {
            return fail(p, "this address expression cannot be stored in an object file").value;
        }
        if (!p->relocatable) {
            return fail(p, "an address of an object file cannot be used here, only a constant").value;
        }
        p->base = value.base;
    }
    return value.value;
}
