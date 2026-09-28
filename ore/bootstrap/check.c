#include <stdio.h>
#include <string.h>
#include "ore.h"

#define error(c, line, ...) fail((c)->m, (line), __VA_ARGS__)

// Values larger than this cannot be laid out in the VM's memory anyway
#define MAX_SIZE (16 * 1024 * 1024)

static Type t_void = { .kind = TY_VOID, .name = "nothing" };
static Type t_bool = { .kind = TY_BOOL, .size = 1, .align = 1, .name = "bool" };
static Type t_i8 = { .kind = TY_INT, .size = 1, .align = 1, .is_signed = 1, .name = "i8" };
static Type t_i16 = { .kind = TY_INT, .size = 2, .align = 2, .is_signed = 1, .name = "i16" };
static Type t_i32 = { .kind = TY_INT, .size = 4, .align = 4, .is_signed = 1, .name = "int" };
static Type t_u8 = { .kind = TY_INT, .size = 1, .align = 1, .name = "u8" };
static Type t_u16 = { .kind = TY_INT, .size = 2, .align = 2, .name = "u16" };
static Type t_u32 = { .kind = TY_INT, .size = 4, .align = 4, .name = "u32" };
static Type t_f32 = { .kind = TY_FLOAT, .size = 4, .align = 4, .name = "f32" };
static Type t_null = { .kind = TY_NULL, .size = 4, .align = 4, .name = "null" };
static Type t_untyped = { .kind = TY_UNTYPED, .size = 4, .align = 4, .name = "a number" };
static Field syscall_fields[] = { { "value", &t_i32, 0, 0 }, { "status", &t_i32, 4, 0 } };
static Type t_syscall = { .kind = TY_STRUCT, .size = 8, .align = 4, .name = "SyscallResult",
                          .fields = syscall_fields, .field_count = 2, .state = 2 };

Type *type_void = &t_void, *type_bool = &t_bool, *type_int = &t_i32, *type_u8 = &t_u8, *type_u32 = &t_u32,
     *type_syscall = &t_syscall;

static Symbol universe[] = {
    { .kind = SYM_TYPE, .name = "i8", .type = &t_i8 },
    { .kind = SYM_TYPE, .name = "i16", .type = &t_i16 },
    { .kind = SYM_TYPE, .name = "i32", .type = &t_i32 },
    { .kind = SYM_TYPE, .name = "int", .type = &t_i32 },
    { .kind = SYM_TYPE, .name = "u8", .type = &t_u8 },
    { .kind = SYM_TYPE, .name = "u16", .type = &t_u16 },
    { .kind = SYM_TYPE, .name = "u32", .type = &t_u32 },
    { .kind = SYM_TYPE, .name = "f32", .type = &t_f32 },
    { .kind = SYM_TYPE, .name = "bool", .type = &t_bool },
    { .kind = SYM_TYPE, .name = "SyscallResult", .type = &t_syscall },
    { .kind = SYM_BUILTIN, .name = "print", .builtin = BUILTIN_PRINT },
    { .kind = SYM_BUILTIN, .name = "panic", .builtin = BUILTIN_PANIC },
    { .kind = SYM_BUILTIN, .name = "assert", .builtin = BUILTIN_ASSERT },
    { .kind = SYM_BUILTIN, .name = "free", .builtin = BUILTIN_FREE },
    { .kind = SYM_BUILTIN, .name = "syscall", .builtin = BUILTIN_SYSCALL },
};

typedef struct {
    Program *program;
    Module *m;
    Decl *fn;
    Symbol **locals;
    int local_count;
    int local_capacity;
    int function_start;     // the first local of the current function
    int loops;
} Checker;

static Type *check_expr(Checker *c, Expr *e);
static Type *resolve_type(Checker *c, TypeExpr *t);
static void check_const(Checker *c, Decl *d);

static Type *new_type(TypeKind kind, Type *base) {
    Type *t = allocate(sizeof(Type));
    t->kind = kind;
    t->base = base;
    t->size = kind == TY_SLICE ? 8 : 4;
    t->align = 4;
    return t;
}

int same_type(const Type *a, const Type *b) {
    if (a == b) {
        return 1;
    }
    if (a->kind != b->kind) {
        return 0;
    }
    switch (a->kind) {
        case TY_POINTER:
        case TY_SLICE:
            return same_type(a->base, b->base);
        case TY_ARRAY:
            return a->length == b->length && same_type(a->base, b->base);
        case TY_FN:
            if (a->param_count != b->param_count || a->is_interrupt != b->is_interrupt ||
                a->is_extern != b->is_extern || !same_type(a->base, b->base)) {
                return 0;
            }
            for (int i = 0; i < a->param_count; i++) {
                if (!same_type(a->params[i], b->params[i])) {
                    return 0;
                }
            }
            return 1;
        default:
            return 0;
    }
}

const char *type_name(const Type *t) {
    char text[256];
    switch (t->kind) {
        case TY_POINTER:
            snprintf(text, sizeof(text), "*%s", type_name(t->base));
            break;
        case TY_SLICE:
            snprintf(text, sizeof(text), "[]%s", type_name(t->base));
            break;
        case TY_ARRAY:
            snprintf(text, sizeof(text), "[%d]%s", t->length, type_name(t->base));
            break;
        case TY_FN: {
            size_t used = (size_t)snprintf(text, sizeof(text), "%sfn(", t->is_extern ? "extern " : "");
            for (int i = 0; i < t->param_count && used < sizeof(text); i++) {
                used += (size_t)snprintf(text + used, sizeof(text) - used, "%s%s", i ? ", " : "",
                                         type_name(t->params[i]));
            }
            if (used < sizeof(text)) {
                snprintf(text + used, sizeof(text) - used, ")%s%s", t->base->kind == TY_VOID ? "" : " ",
                         t->base->kind == TY_VOID ? "" : type_name(t->base));
            }
            break;
        }
        default:
            return t->name;
    }
    return copy_text(text, strlen(text));
}

int is_scalar(const Type *t) {
    return t->kind == TY_BOOL || t->kind == TY_INT || t->kind == TY_FLOAT || t->kind == TY_POINTER ||
           t->kind == TY_FN || t->kind == TY_ENUM || t->kind == TY_NULL || t->kind == TY_UNTYPED;
}

static int is_integer(const Type *t) {
    return t->kind == TY_INT || t->kind == TY_UNTYPED;
}

// The value a sized integer holds after an operation, as the machine computes it
static int64_t wrap(int64_t value, const Type *t) {
    if (t->kind != TY_INT) {
        return value;
    }
    switch (t->size) {
        case 1:
            return t->is_signed ? (int64_t)(int8_t)value : (int64_t)(uint8_t)value;
        case 2:
            return t->is_signed ? (int64_t)(int16_t)value : (int64_t)(uint16_t)value;
        default:
            return t->is_signed ? (int64_t)(int32_t)value : (int64_t)(uint32_t)value;
    }
}

// Every value of from is also a value of to
static int widens(const Type *from, const Type *to) {
    if (from->kind == TY_INT && to->kind == TY_FLOAT) {
        return from->size <= 2;
    }
    if (from->kind != TY_INT || to->kind != TY_INT || to->size <= from->size) {
        return 0;
    }
    return to->is_signed || !from->is_signed;
}

// f32 constants keep their bits
static float to_float(int64_t value) {
    uint32_t bits = (uint32_t)value;
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

static int64_t float_bits(float f) {
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

// Conversions of constants between integers and f32, as the machine code for them computes them
static int64_t int_to_float(int64_t value) {
    return float_bits((float)value);
}

static int32_t truncate_float(float f) {
    return f >= -2147483648.0f && f < 2147483648.0f ? (int32_t)f : INT32_MIN;
}

static int64_t float_to_int(int64_t value, const Type *to) {
    float f = to_float(value);
    if (to->size == 4 && !to->is_signed && f >= 2147483648.0f) {
        return (uint32_t)truncate_float(f - 2147483648.0f) ^ 0x80000000u;
    }
    return wrap(truncate_float(f), to);
}

static Expr *new_expr(ExprKind kind, int line) {
    Expr *e = allocate(sizeof(Expr));
    e->kind = kind;
    e->line = line;
    return e;
}

static void layout(Checker *c, Type *t, int line) {
    if (t->kind == TY_ARRAY) {
        layout(c, t->base, line);
        return;
    }
    if (t->kind != TY_STRUCT || t->state == 2) {
        return;
    }
    if (t->state == 1) {
        error(c, line, "struct %s contains itself", t->name);
    }
    t->state = 1;
    Module *saved = c->m;
    int saved_locals = c->local_count;
    c->m = t->decl->module;
    c->local_count = 0;
    Decl *d = t->decl;
    t->field_count = d->param_count;
    t->fields = allocate((size_t)d->param_count * sizeof(Field));
    for (int i = 0; i < d->param_count; i++) {
        for (int k = 0; k < i; k++) {
            if (strcmp(d->params[k].name, d->params[i].name) == 0) {
                error(c, d->params[i].line, "field %s is declared twice", d->params[i].name);
            }
        }
        t->fields[i] = (Field){ d->params[i].name, resolve_type(c, d->params[i].type_expr), 0, d->params[i].line };
    }
    int offset = 0, align = 1;
    for (int i = 0; i < t->field_count; i++) {
        Field *f = &t->fields[i];
        layout(c, f->type, f->line);
        if (f->type->kind == TY_VOID) {
            error(c, f->line, "a field cannot have the type %s", type_name(f->type));
        }
        offset = (offset + f->type->align - 1) / f->type->align * f->type->align;
        f->offset = offset;
        offset += f->type->size;
        align = f->type->align > align ? f->type->align : align;
        if (offset > MAX_SIZE) {
            error(c, f->line, "struct %s is too large", t->name);
        }
    }
    c->m = saved;
    c->local_count = saved_locals;
    t->align = align;
    t->size = (offset + align - 1) / align * align;
    t->state = 2;
}

static Symbol *find(Symbol **symbols, int count, const char *name) {
    for (int i = count; i-- > 0;) {
        if (strcmp(symbols[i]->name, name) == 0) {
            return symbols[i];
        }
    }
    return NULL;
}

static Symbol *find_universe(const char *name) {
    for (size_t i = 0; i < sizeof(universe) / sizeof(universe[0]); i++) {
        if (strcmp(universe[i].name, name) == 0) {
            return &universe[i];
        }
    }
    return NULL;
}

static Symbol *lookup(Checker *c, const char *name) {
    Symbol *s = find(c->locals, c->local_count, name);
    if (!s) {
        s = find(c->m->symbols, c->m->symbol_count, name);
    }
    return s ? s : find_universe(name);
}

static Symbol *member(Checker *c, Module *m, const char *name, int line) {
    Symbol *s = find(m->symbols, m->symbol_count, name);
    if (!s || s->kind == SYM_MODULE) {
        error(c, line, "module %s has no %s", m->name, name);
    }
    if (m != c->m && !s->decl->is_pub) {
        error(c, line, "%s.%s is not pub", m->name, name);
    }
    return s;
}

static Symbol *type_symbol(Checker *c, TypeExpr *t) {
    Symbol *s;
    if (t->qualifier) {
        s = lookup(c, t->qualifier);
        if (!s || s->kind != SYM_MODULE) {
            error(c, t->line, "%s is not a module", t->qualifier);
        }
        s = member(c, s->module, t->name, t->line);
    } else {
        s = lookup(c, t->name);
    }
    if (!s || s->kind != SYM_TYPE) {
        error(c, t->line, "%s is not a type", t->name);
    }
    return s;
}

static int64_t constant_int(Checker *c, Expr *e, const char *what) {
    Type *t = check_expr(c, e);
    if (!e->is_const || !is_integer(t)) {
        error(c, e->line, "%s must be an integer constant", what);
    }
    return e->value;
}

static Type *resolve_type(Checker *c, TypeExpr *te) {
    Type *t;
    switch (te->kind) {
        case TE_NAME:
            return type_symbol(c, te)->type;
        case TE_POINTER:
            return new_type(TY_POINTER, resolve_type(c, te->base));
        case TE_SLICE:
            t = new_type(TY_SLICE, resolve_type(c, te->base));
            if (t->base->kind == TY_VOID) {
                error(c, te->line, "a slice cannot hold %s", type_name(t->base));
            }
            return t;
        case TE_ARRAY: {
            int64_t length = constant_int(c, te->length, "the length of an array");
            t = new_type(TY_ARRAY, resolve_type(c, te->base));
            layout(c, t->base, te->line);
            if (length < 0 || (t->base->size && length > MAX_SIZE / t->base->size)) {
                error(c, te->line, "invalid length %lld for an array", (long long)length);
            }
            t->length = (int)length;
            t->size = t->length * t->base->size;
            t->align = t->base->align;
            return t;
        }
        case TE_FN:
            t = new_type(TY_FN, te->base ? resolve_type(c, te->base) : &t_void);
            t->param_count = te->param_count;
            t->params = allocate((size_t)te->param_count * sizeof(Type *));
            for (int i = 0; i < te->param_count; i++) {
                t->params[i] = resolve_type(c, te->params[i]);
            }
            return t;
    }
    return &t_void;
}

// A type that a variable, field or parameter can have
static Type *value_type(Checker *c, TypeExpr *te) {
    Type *t = resolve_type(c, te);
    layout(c, t, te->line);
    if (t->kind == TY_VOID) {
        error(c, te->line, "%s is not a type for values", type_name(t));
    }
    return t;
}

// Where a value meets another type: untyped constants take it if they fit, and only conversions that
// lose nothing happen by themselves
static void coerce(Checker *c, Expr **slot, Type *to) {
    Expr *e = *slot;
    Type *from = e->type;

    if (same_type(from, to)) {
        return;
    }
    if (from->kind == TY_UNTYPED && to->kind == TY_INT) {
        if (wrap(e->value, to) != e->value) {
            error(c, e->line, "%lld does not fit in %s", (long long)e->value, type_name(to));
        }
        e->type = to;
        return;
    }
    if (e->is_const && is_integer(from) && to->kind == TY_FLOAT && (from->kind == TY_UNTYPED || widens(from, to))) {
        e->value = int_to_float(e->value);
        e->type = to;
        return;
    }
    if (from->kind == TY_NULL && (to->kind == TY_POINTER || to->kind == TY_FN)) {
        e->type = to;
        return;
    }
    if (widens(from, to) || (from->kind == TY_ARRAY && to->kind == TY_SLICE && same_type(from->base, to->base))) {
        if (e->is_const) {
            e->type = to;
            return;
        }
        Expr *conversion = new_expr(EX_CONVERT, e->line);
        conversion->left = e;
        conversion->type = to;
        *slot = conversion;
        return;
    }
    error(c, e->line, "expected %s, found %s", type_name(to), type_name(from));
}

// Untyped constants become int where nothing else decides their type
static Type *settle(Checker *c, Expr **slot) {
    if ((*slot)->type->kind == TY_UNTYPED) {
        coerce(c, slot, &t_i32);
    }
    return (*slot)->type;
}

static int addressable(const Expr *e) {
    switch (e->kind) {
        case EX_NAME:
            return e->symbol->kind == SYM_LOCAL || e->symbol->kind == SYM_GLOBAL;
        case EX_INDEX:
            return e->left->type->kind != TY_ARRAY || addressable(e->left);
        case EX_FIELD:
            return e->left->type->kind == TY_POINTER || addressable(e->left);
        case EX_UNARY:
            return e->op == TOK_STAR;
        default:
            return 0;
    }
}

static void declare_local(Checker *c, Symbol *s) {
    Symbol *old = find(c->locals + c->function_start, c->local_count - c->function_start, s->name);
    if (old) {
        error(c, s->line, "%s is already declared at line %d", s->name, old->line);
    }
    if (strcmp(s->name, "new") == 0 || strcmp(s->name, "sizeof") == 0) {
        error(c, s->line, "%s is a builtin", s->name);
    }
    extend((void **)&c->locals, &c->local_capacity, c->local_count, sizeof(Symbol *));
    c->locals[c->local_count++] = s;
}

static int64_t fold_binary(Checker *c, Expr *e, int64_t a, int64_t b, Type *t) {
    uint64_t ua = (uint64_t)a, ub = (uint64_t)b;
    int64_t result = 0;
    int untyped = t->kind == TY_UNTYPED, overflow = 0;

    switch (e->op) {
        case TOK_PLUS:
            overflow = __builtin_add_overflow(a, b, &result);
            result = untyped ? result : (int64_t)(ua + ub);
            break;
        case TOK_MINUS:
            overflow = __builtin_sub_overflow(a, b, &result);
            result = untyped ? result : (int64_t)(ua - ub);
            break;
        case TOK_STAR:
            overflow = __builtin_mul_overflow(a, b, &result);
            result = untyped ? result : (int64_t)(ua * ub);
            break;
        case TOK_SLASH:
        case TOK_PERCENT:
            if (b == 0) {
                error(c, e->line, "division by zero");
            }
            if (untyped ? a == INT64_MIN && b == -1 : t->is_signed && t->size == 4 && a == INT32_MIN && b == -1) {
                error(c, e->line, "the division overflows");
            }
            result = e->op == TOK_SLASH ? a / b : a % b;
            break;
        case TOK_SHL:
            if (untyped) {
                if (b < 0 || b > 62 || a > (INT64_MAX >> b) || a < (INT64_MIN >> b)) {
                    error(c, e->line, "the shift overflows");
                }
                result = (int64_t)(ua << b);
            } else {
                result = (int64_t)(ua << (b & 31));
            }
            break;
        case TOK_SHR:
            if (untyped) {
                if (b < 0 || b > 63) {
                    error(c, e->line, "invalid shift count %lld", (long long)b);
                }
                result = a >> b;
            } else {
                result = t->is_signed ? a >> (b & 31) : (int64_t)(ua >> (b & 31));
            }
            break;
        case TOK_AMP:
            result = a & b;
            break;
        case TOK_PIPE:
            result = a | b;
            break;
        case TOK_CARET:
            result = a ^ b;
            break;
        case TOK_EQ:
            return a == b;
        case TOK_NE:
            return a != b;
        case TOK_LT:
            return a < b;
        case TOK_LE:
            return a <= b;
        case TOK_GT:
            return a > b;
        case TOK_GE:
            return a >= b;
        case TOK_ANDAND:
            return a && b;
        case TOK_OROR:
            return a || b;
        default:
            break;
    }
    if (untyped && overflow) {
        error(c, e->line, "the constant overflows");
    }
    return wrap(result, t);
}

static int64_t fold_float(TokenKind op, int64_t left, int64_t right) {
    float a = to_float(left), b = to_float(right);
    switch (op) {
        case TOK_PLUS:
            return float_bits(a + b);
        case TOK_MINUS:
            return float_bits(a - b);
        case TOK_STAR:
            return float_bits(a * b);
        case TOK_SLASH:
            return float_bits(a / b);
        case TOK_EQ:
            return a == b;
        case TOK_NE:
            return a != b;
        case TOK_LT:
            return a < b;
        case TOK_LE:
            return a <= b;
        case TOK_GT:
            return a > b;
        default:
            return a >= b;
    }
}

// Gives both operands one type: an untyped constant takes the other's type, and a smaller integer type
// widens to a larger one that holds all of its values
static Type *unify(Checker *c, Expr *e) {
    Type *l = e->left->type, *r = e->right->type;
    if (l->kind == TY_UNTYPED && r->kind == TY_UNTYPED) {
        return l;
    }
    if (l->kind == TY_UNTYPED || l->kind == TY_NULL || widens(l, r)) {
        coerce(c, &e->left, r);
        return r;
    }
    if (r->kind == TY_UNTYPED || r->kind == TY_NULL || widens(r, l)) {
        coerce(c, &e->right, l);
        return l;
    }
    if (!same_type(l, r)) {
        error(c, e->line, "mismatched types %s and %s", type_name(l), type_name(r));
    }
    return l;
}

// The operator's rules, once both operands are checked
static Type *binary_type(Checker *c, Expr *e) {
    Type *l = e->left->type;
    Type *r = e->right->type;
    Type *t;

    switch (e->op) {
        case TOK_ANDAND:
        case TOK_OROR:
            coerce(c, &e->left, &t_bool);
            coerce(c, &e->right, &t_bool);
            t = &t_bool;
            break;
        case TOK_EQ:
        case TOK_NE:
        case TOK_LT:
        case TOK_LE:
        case TOK_GT:
        case TOK_GE:
            t = unify(c, e);
            if (t->kind == TY_NULL) {
                error(c, e->line, "cannot compare null with null");
            }
            if (!is_scalar(t)) {
                error(c, e->line, "%s values cannot be compared; the standard library compares their contents",
                      type_name(t));
            }
            if (e->op != TOK_EQ && e->op != TOK_NE && t->kind != TY_INT && t->kind != TY_UNTYPED &&
                t->kind != TY_POINTER && t->kind != TY_FLOAT) {
                error(c, e->line, "%s values only compare with == and !=", type_name(t));
            }
            if (e->left->is_const && e->right->is_const) {
                e->value = t->kind == TY_FLOAT ? fold_float(e->op, e->left->value, e->right->value)
                                               : fold_binary(c, e, e->left->value, e->right->value, t);
                e->is_const = 1;
            }
            return e->type = &t_bool;
        case TOK_SHL:
        case TOK_SHR:
            if (!is_integer(l) || !is_integer(r)) {
                error(c, e->line, "shifts take integers, not %s and %s", type_name(l), type_name(r));
            }
            if (!e->right->is_const) {
                settle(c, &e->right);
            }
            t = l->kind == TY_UNTYPED && !e->right->is_const ? settle(c, &e->left) : l;
            break;
        case TOK_PLUS:
        case TOK_MINUS:
            if (l->kind == TY_POINTER && (e->op == TOK_PLUS || r->kind != TY_POINTER)) {
                coerce(c, &e->right, &t_i32);
                return e->type = l;
            }
            if (l->kind == TY_POINTER && e->op == TOK_MINUS) {
                if (!same_type(l, r)) {
                    error(c, e->line, "mismatched types %s and %s", type_name(l), type_name(r));
                }
                return e->type = &t_i32;
            }
            // fall through
        default:
            t = unify(c, e);
            if (t->kind == TY_FLOAT && e->op >= TOK_PLUS && e->op <= TOK_SLASH) {
                if (e->left->is_const && e->right->is_const) {
                    e->value = fold_float(e->op, e->left->value, e->right->value);
                    e->is_const = 1;
                }
                return e->type = t;
            }
            if (!is_integer(t)) {
                error(c, e->line, "%s needs integers, not %s", token_text(e->op), type_name(t));
            }
            break;
    }
    if (e->left->is_const && e->right->is_const) {
        e->value = fold_binary(c, e, e->left->value, e->right->value, t);
        e->is_const = 1;
    }
    return e->type = t;
}

static Type *check_binary(Checker *c, Expr *e) {
    check_expr(c, e->left);
    check_expr(c, e->right);
    return binary_type(c, e);
}

static Type *check_unary(Checker *c, Expr *e) {
    Type *t = check_expr(c, e->left);
    switch (e->op) {
        case TOK_MINUS:
        case TOK_TILDE:
            if (t->kind == TY_FLOAT && e->op == TOK_MINUS) {
                e->is_const = e->left->is_const;
                e->value = e->left->value ^ 0x80000000u;
                return e->type = t;
            }
            if (!is_integer(t)) {
                error(c, e->line, "%s needs an integer, not %s", token_text(e->op), type_name(t));
            }
            if (e->left->is_const) {
                if (t->kind == TY_UNTYPED && e->op == TOK_MINUS && e->left->value == INT64_MIN) {
                    error(c, e->line, "the constant overflows");
                }
                uint64_t v = (uint64_t)e->left->value;
                e->value = wrap((int64_t)(e->op == TOK_MINUS ? 0 - v : ~v), t);
                e->is_const = 1;
            }
            return e->type = t;
        case TOK_BANG:
            coerce(c, &e->left, &t_bool);
            if (e->left->is_const) {
                e->value = !e->left->value;
                e->is_const = 1;
            }
            return e->type = &t_bool;
        case TOK_STAR:
            if (t->kind != TY_POINTER) {
                error(c, e->line, "only pointers can be dereferenced, not %s", type_name(t));
            }
            layout(c, t->base, e->line);
            if (t->base->kind == TY_VOID) {
                error(c, e->line, "cannot dereference %s", type_name(t));
            }
            return e->type = t->base;
        default:
            if (!addressable(e->left)) {
                error(c, e->line, "cannot take the address of this");
            }
            return e->type = new_type(TY_POINTER, t);
    }
}

// A name or module.name that refers to a module, a type or anything declared
static Symbol *named(Checker *c, Expr *e) {
    if (e->kind == EX_NAME) {
        return lookup(c, e->name);
    }
    if (e->kind == EX_FIELD && e->left->kind == EX_NAME) {
        Symbol *s = lookup(c, e->left->name);
        if (s && s->kind == SYM_MODULE) {
            return member(c, s->module, e->name, e->line);
        }
    }
    return NULL;
}

static Type *use_symbol(Checker *c, Expr *e, Symbol *s) {
    e->kind = EX_NAME;
    e->symbol = s;
    switch (s->kind) {
        case SYM_CONST:
            if (s->decl) {
                check_const(c, s->decl);
            }
            e->is_const = s->string == NULL;
            e->value = s->value;
            return e->type = s->type;
        case SYM_LOCAL:
        case SYM_GLOBAL:
        case SYM_FN:
            return e->type = s->type;
        case SYM_TYPE:
            error(c, e->line, "%s is a type, not a value", s->name);
        case SYM_MODULE:
            error(c, e->line, "%s is a module, not a value", s->name);
        case SYM_BUILTIN:
            error(c, e->line, "%s can only be called", s->name);
    }
    return NULL;
}

static Type *check_field(Checker *c, Expr *e) {
    Symbol *s = named(c, e->left);
    if (s && s->kind == SYM_MODULE) {
        return use_symbol(c, e, member(c, s->module, e->name, e->line));
    }
    if (s && s->kind == SYM_TYPE) {
        Type *t = s->type;
        if (t->kind != TY_ENUM) {
            error(c, e->line, "%s is a type, not a value", type_name(t));
        }
        for (int i = 0; i < t->decl->member_count; i++) {
            if (strcmp(t->decl->members[i].name, e->name) == 0) {
                e->is_const = 1;
                e->value = t->decl->members[i].number;
                return e->type = t;
            }
        }
        error(c, e->line, "enum %s has no value %s", t->name, e->name);
    }

    Type *t = check_expr(c, e->left);
    if (t->kind == TY_POINTER && t->base->kind == TY_STRUCT) {
        t = t->base;
    }
    if (t->kind == TY_STRUCT) {
        layout(c, t, e->line);
        for (int i = 0; i < t->field_count; i++) {
            if (strcmp(t->fields[i].name, e->name) == 0) {
                e->field = &t->fields[i];
                return e->type = e->field->type;
            }
        }
    } else if (t->kind == TY_SLICE && (strcmp(e->name, "len") == 0 || strcmp(e->name, "ptr") == 0)) {
        e->kind = e->name[0] == 'l' ? EX_LEN : EX_PTR;
        return e->type = e->kind == EX_LEN ? &t_i32 : new_type(TY_POINTER, t->base);
    } else if (t->kind == TY_ARRAY && strcmp(e->name, "len") == 0) {
        e->is_const = 1;
        e->value = t->length;
        return e->type = &t_untyped;
    }
    error(c, e->line, "%s has no field %s", type_name(t), e->name);
}

static void check_args(Checker *c, Expr *e, Type *fn) {
    if (e->arg_count != fn->param_count) {
        error(c, e->line, "the function takes %d argument%s, not %d", fn->param_count,
              fn->param_count == 1 ? "" : "s", e->arg_count);
    }
    for (int i = 0; i < e->arg_count; i++) {
        check_expr(c, e->args[i]);
        coerce(c, &e->args[i], fn->params[i]);
    }
}

static Type *check_builtin(Checker *c, Expr *e, Builtin builtin) {
    e->builtin = builtin;
    for (int i = 0; i < e->arg_count; i++) {
        check_expr(c, e->args[i]);
    }
    switch (builtin) {
        case BUILTIN_PRINT:
            for (int i = 0; i < e->arg_count; i++) {
                Type *t = settle(c, &e->args[i]);
                if (t->kind == TY_ARRAY && same_type(t->base, &t_u8)) {
                    coerce(c, &e->args[i], new_type(TY_SLICE, &t_u8));
                    t = e->args[i]->type;
                }
                if (!is_scalar(t) && !(t->kind == TY_SLICE && same_type(t->base, &t_u8))) {
                    error(c, e->args[i]->line, "print cannot show %s", type_name(t));
                }
                if (t->kind == TY_NULL) {
                    error(c, e->args[i]->line, "print cannot show null");
                }
            }
            return e->type = &t_void;
        case BUILTIN_PANIC:
        case BUILTIN_ASSERT:
        case BUILTIN_FREE:
            if (e->arg_count != 1) {
                error(c, e->line, "%s takes one argument", e->left->name);
            }
            if (builtin == BUILTIN_PANIC) {
                coerce(c, &e->args[0], new_type(TY_SLICE, &t_u8));
            } else if (builtin == BUILTIN_ASSERT) {
                coerce(c, &e->args[0], &t_bool);
            } else if (e->args[0]->type->kind != TY_POINTER && e->args[0]->type->kind != TY_SLICE) {
                error(c, e->line, "free takes a pointer or a slice from new, not %s", type_name(e->args[0]->type));
            }
            return e->type = &t_void;
        case BUILTIN_SYSCALL:
            if (e->arg_count < 1 || e->arg_count > 4) {
                error(c, e->line, "syscall takes a syscall number and up to three values");
            }
            if (!e->args[0]->is_const || !is_integer(e->args[0]->type) || e->args[0]->value < 0 ||
                e->args[0]->value > 255) {
                error(c, e->line, "the syscall number must be a constant from 0 to 255");
            }
            for (int i = 1; i < e->arg_count; i++) {
                Type *t = settle(c, &e->args[i]);
                if (t->kind != TY_INT && t->kind != TY_FLOAT && t->kind != TY_POINTER && t->kind != TY_BOOL &&
                    t->kind != TY_ENUM) {
                    error(c, e->args[i]->line, "syscall takes integers, floats and pointers, not %s", type_name(t));
                }
            }
            return e->type = &t_syscall;
    }
    return &t_void;
}

static Type *check_call(Checker *c, Expr *e) {
    Symbol *s = named(c, e->left);
    if (s && s->kind == SYM_BUILTIN) {
        e->left->symbol = s;
        return check_builtin(c, e, s->builtin);
    }
    Type *t = check_expr(c, e->left);
    if (t->kind != TY_FN) {
        error(c, e->line, "%s is not a function", type_name(t));
    }
    if (t->is_interrupt) {
        error(c, e->line, "an interrupt function cannot be called");
    }
    check_args(c, e, t);
    return e->type = t->base;
}

static Type *check_index(Checker *c, Expr *e) {
    Type *t = check_expr(c, e->left);
    if (t->kind != TY_ARRAY && t->kind != TY_SLICE && t->kind != TY_POINTER) {
        error(c, e->line, "only arrays, slices and pointers can be indexed, not %s", type_name(t));
    }
    if (e->right) {
        check_expr(c, e->right);
        coerce(c, &e->right, &t_i32);
    }
    if (e->third) {
        check_expr(c, e->third);
        coerce(c, &e->third, &t_i32);
    }
    int64_t start = e->right && e->right->is_const ? e->right->value : 0;
    int64_t end = e->third && e->third->is_const ? e->third->value : t->length;
    if (t->kind == TY_ARRAY && e->kind == EX_INDEX && e->right->is_const && (start < 0 || start >= t->length)) {
        error(c, e->line, "index %lld is out of bounds for length %d", (long long)start, t->length);
    }
    if (e->kind == EX_INDEX) {
        layout(c, t->base, e->line);
        return e->type = t->base;
    }
    if (t->kind == TY_POINTER && !e->third) {
        error(c, e->line, "slicing a pointer needs an end");
    }
    if (t->kind == TY_ARRAY) {
        if (!addressable(e->left)) {
            error(c, e->line, "only an array stored in a variable can be sliced");
        }
        if ((!e->right || e->right->is_const) && (!e->third || e->third->is_const) &&
            (start < 0 || start > end || end > t->length)) {
            error(c, e->line, "slice %lld:%lld is out of bounds for length %d", (long long)start, (long long)end,
                  t->length);
        }
    }
    return e->type = new_type(TY_SLICE, t->base);
}

static Type *check_cast(Checker *c, Expr *e) {
    Type *from = check_expr(c, e->left);
    Type *to = e->named = value_type(c, e->type_expr);
    int ok = same_type(from, to);

    if (is_integer(from) && to->kind == TY_INT) {
        ok = 1;
        if (e->left->is_const) {
            e->is_const = 1;
            e->value = wrap(e->left->value, to);
        }
    } else if (is_integer(from) && to->kind == TY_FLOAT) {
        ok = 1;
        e->is_const = e->left->is_const;
        e->value = int_to_float(e->left->value);
    } else if (from->kind == TY_FLOAT && to->kind == TY_INT) {
        ok = 1;
        e->is_const = e->left->is_const;
        e->value = float_to_int(e->left->value, to);
    } else if (from->kind == TY_FLOAT && to->kind == TY_FLOAT) {
        e->is_const = e->left->is_const;
        e->value = e->left->value;
    } else if ((from->kind == TY_BOOL || from->kind == TY_ENUM) && to->kind == TY_INT) {
        ok = 1;
        e->is_const = e->left->is_const;
        e->value = wrap(e->left->value, to);
    } else if (is_integer(from) && to->kind == TY_ENUM) {
        ok = 1;
        settle(c, &e->left);
        e->is_const = e->left->is_const;
        e->value = e->left->value;
    } else if ((from->kind == TY_POINTER || from->kind == TY_FN) && to->kind == TY_INT && to->size == 4) {
        ok = 1;
    } else if (is_integer(from) && to->kind == TY_POINTER) {
        ok = 1;
        settle(c, &e->left);
    } else if ((from->kind == TY_POINTER || from->kind == TY_NULL) && to->kind == TY_POINTER) {
        ok = 1;
    }
    if (!ok) {
        error(c, e->line, "cannot convert %s to %s", type_name(from), type_name(to));
    }
    return e->type = to;
}

static Type *check_literal(Checker *c, Expr *e) {
    if (e->kind == EX_STRUCT) {
        Type *t = e->named = type_symbol(c, e->type_expr)->type;
        if (t->kind != TY_STRUCT) {
            error(c, e->line, "%s is not a struct", type_name(t));
        }
        layout(c, t, e->line);
        for (int i = 0; i < e->arg_count; i++) {
            int found = -1;
            for (int k = 0; k < t->field_count; k++) {
                found = strcmp(t->fields[k].name, e->fields[i]) == 0 ? k : found;
            }
            if (found < 0) {
                error(c, e->args[i]->line, "%s has no field %s", t->name, e->fields[i]);
            }
            for (int k = 0; k < i; k++) {
                if (strcmp(e->fields[k], e->fields[i]) == 0) {
                    error(c, e->args[i]->line, "field %s is given twice", e->fields[i]);
                }
            }
            check_expr(c, e->args[i]);
            coerce(c, &e->args[i], t->fields[found].type);
        }
        return e->type = t;
    }

    Type *t = e->named = value_type(c, e->type_expr);
    if (t->kind != TY_ARRAY && t->kind != TY_SLICE) {
        error(c, e->line, "%s is not an array or slice", type_name(t));
    }
    if (t->kind == TY_ARRAY && e->arg_count > t->length) {
        error(c, e->line, "%d values do not fit in %s", e->arg_count, type_name(t));
    }
    layout(c, t->base, e->line);
    for (int i = 0; i < e->arg_count; i++) {
        check_expr(c, e->args[i]);
        coerce(c, &e->args[i], t->base);
    }
    return e->type = t;
}

static Type *check_expr(Checker *c, Expr *e) {
    switch (e->kind) {
        case EX_INT:
            e->is_const = 1;
            return e->type = &t_untyped;
        case EX_FLOAT:
            e->is_const = 1;
            return e->type = &t_f32;
        case EX_BOOL:
            e->is_const = 1;
            return e->type = &t_bool;
        case EX_NULL:
            return e->type = &t_null;
        case EX_STRING:
            return e->type = new_type(TY_SLICE, &t_u8);
        case EX_NAME: {
            Symbol *s = lookup(c, e->name);
            if (!s) {
                if (strcmp(e->name, "new") == 0 || strcmp(e->name, "sizeof") == 0) {
                    error(c, e->line, "%s can only be called", e->name);
                }
                error(c, e->line, "%s is not declared", e->name);
            }
            return use_symbol(c, e, s);
        }
        case EX_UNARY:
            return check_unary(c, e);
        case EX_BINARY:
            return check_binary(c, e);
        case EX_CALL:
            return check_call(c, e);
        case EX_INDEX:
        case EX_SLICE:
            return check_index(c, e);
        case EX_FIELD:
            return check_field(c, e);
        case EX_CAST:
            return check_cast(c, e);
        case EX_STRUCT:
        case EX_ARRAY:
            return check_literal(c, e);
        case EX_SIZEOF: {
            Type *t = value_type(c, e->type_expr);
            e->is_const = 1;
            e->value = t->size;
            return e->type = &t_untyped;
        }
        case EX_NEW:
            e->named = value_type(c, e->type_expr);
            if (e->left) {
                check_expr(c, e->left);
                coerce(c, &e->left, &t_i32);
                return e->type = new_type(TY_SLICE, e->named);
            }
            return e->type = new_type(TY_POINTER, e->named);
        default:
            return e->type;
    }
}

// The type a variable takes from its initial value
static Type *infer(Checker *c, Expr **slot, int line) {
    Type *t = settle(c, slot);
    if (t->kind == TY_NULL) {
        error(c, line, "the type of null has to be written out");
    }
    if (t->kind == TY_VOID) {
        error(c, line, "this has no value");
    }
    if (t->kind == TY_FN && t->is_interrupt) {
        error(c, line, "an interrupt function can only be converted to u32");
    }
    if (t->kind == TY_FN && t->is_extern) {
        error(c, line, "an extern function can only be called or converted to u32");
    }
    return t;
}

static void check_stmt(Checker *c, Stmt *s);

static void check_block(Checker *c, Stmt *s) {
    int saved = c->local_count;
    for (int i = 0; i < s->count; i++) {
        check_stmt(c, s->body[i]);
    }
    c->local_count = saved;
}

static void check_variable(Checker *c, Stmt *s) {
    Symbol *sym = allocate(sizeof(Symbol));
    sym->name = s->name;
    sym->line = s->line;
    sym->module = c->m;
    if (s->value) {
        check_expr(c, s->value);
    }
    if (s->type_expr) {
        sym->type = value_type(c, s->type_expr);
        if (s->value) {
            coerce(c, &s->value, sym->type);
        }
    } else if (s->kind == ST_CONST) {
        sym->type = s->value->type;
    } else {
        sym->type = infer(c, &s->value, s->line);
    }
    if (s->kind == ST_CONST) {
        if (!s->value->is_const && s->value->kind != EX_STRING) {
            error(c, s->line, "the value of a constant must be known when compiling");
        }
        sym->kind = SYM_CONST;
        sym->value = s->value->value;
        sym->string = s->value->kind == EX_STRING ? s->value : NULL;
    } else {
        sym->kind = SYM_LOCAL;
    }
    s->symbol = sym;
    declare_local(c, sym);
}

static void check_assign(Checker *c, Stmt *s) {
    Type *t = check_expr(c, s->target);
    if (!addressable(s->target)) {
        error(c, s->line, "cannot assign to this");
    }
    check_expr(c, s->value);
    if (s->op == TOK_ASSIGN) {
        coerce(c, &s->value, t);
        return;
    }
    // x op= y is checked like x = x op y
    static const TokenKind ops[] = {
        [TOK_ADD_ASSIGN] = TOK_PLUS, [TOK_SUB_ASSIGN] = TOK_MINUS, [TOK_MUL_ASSIGN] = TOK_STAR,
        [TOK_DIV_ASSIGN] = TOK_SLASH, [TOK_MOD_ASSIGN] = TOK_PERCENT, [TOK_AND_ASSIGN] = TOK_AMP,
        [TOK_OR_ASSIGN] = TOK_PIPE, [TOK_XOR_ASSIGN] = TOK_CARET, [TOK_SHL_ASSIGN] = TOK_SHL,
        [TOK_SHR_ASSIGN] = TOK_SHR,
    };
    Expr e = { .kind = EX_BINARY, .line = s->line, .op = ops[s->op], .left = s->target, .right = s->value };
    Type *result = binary_type(c, &e);
    if (!same_type(result, t)) {
        error(c, s->line, "expected %s, found %s", type_name(t), type_name(result));
    }
    s->value = e.right;
}

static void check_condition(Checker *c, Expr **slot) {
    check_expr(c, *slot);
    coerce(c, slot, &t_bool);
}

static void check_switch(Checker *c, Stmt *s) {
    check_expr(c, s->cond);
    Type *t = settle(c, &s->cond);
    if (t->kind != TY_INT && t->kind != TY_ENUM) {
        error(c, s->line, "a switch takes an integer or enum, not %s", type_name(t));
    }
    for (int i = 0; i < s->case_count; i++) {
        Case *k = &s->cases[i];
        for (int v = 0; v < k->value_count; v++) {
            check_expr(c, k->values[v]);
            coerce(c, &k->values[v], t);
            if (!k->values[v]->is_const) {
                error(c, k->values[v]->line, "a case needs a constant");
            }
            for (int j = 0; j <= i; j++) {
                for (int w = 0; w < (j == i ? v : s->cases[j].value_count); w++) {
                    if (s->cases[j].values[w]->value == k->values[v]->value) {
                        error(c, k->values[v]->line, "case %lld appears twice", (long long)k->values[v]->value);
                    }
                }
            }
        }
        check_block(c, k->body);
    }
    if (s->fallback) {
        check_block(c, s->fallback);
    }
}

static void check_stmt(Checker *c, Stmt *s) {
    switch (s->kind) {
        case ST_BLOCK:
            check_block(c, s);
            break;
        case ST_VAR:
        case ST_CONST:
            check_variable(c, s);
            break;
        case ST_ASSIGN:
            check_assign(c, s);
            break;
        case ST_EXPR:
            check_expr(c, s->value);
            break;
        case ST_IF:
            check_condition(c, &s->cond);
            check_block(c, s->then);
            if (s->otherwise) {
                check_stmt(c, s->otherwise);
            }
            break;
        case ST_WHILE:
            check_condition(c, &s->cond);
            c->loops++;
            check_block(c, s->then);
            c->loops--;
            break;
        case ST_FOR: {
            int saved = c->local_count;
            if (s->init) {
                check_stmt(c, s->init);
            }
            if (s->cond) {
                check_condition(c, &s->cond);
            }
            if (s->step) {
                check_stmt(c, s->step);
            }
            c->loops++;
            check_block(c, s->then);
            c->loops--;
            c->local_count = saved;
            break;
        }
        case ST_SWITCH:
            check_switch(c, s);
            break;
        case ST_BREAK:
        case ST_CONTINUE:
            if (c->loops == 0) {
                error(c, s->line, "%s is only allowed in a loop", s->kind == ST_BREAK ? "break" : "continue");
            }
            break;
        case ST_RETURN: {
            Type *result = c->fn->symbol->type->base;
            if (!s->value) {
                if (result->kind != TY_VOID) {
                    error(c, s->line, "%s has to return %s", c->fn->name, type_name(result));
                }
                break;
            }
            if (result->kind == TY_VOID) {
                error(c, s->line, "%s does not return a value", c->fn->name);
            }
            check_expr(c, s->value);
            coerce(c, &s->value, result);
            break;
        }
    }
}

// A break inside the loop, not inside a loop nested in it
static int breaks(const Stmt *s) {
    if (!s) {
        return 0;
    }
    switch (s->kind) {
        case ST_BREAK:
            return 1;
        case ST_BLOCK:
            for (int i = 0; i < s->count; i++) {
                if (breaks(s->body[i])) {
                    return 1;
                }
            }
            return 0;
        case ST_IF:
            return breaks(s->then) || breaks(s->otherwise);
        case ST_SWITCH:
            for (int i = 0; i < s->case_count; i++) {
                if (breaks(s->cases[i].body)) {
                    return 1;
                }
            }
            return breaks(s->fallback);
        default:
            return 0;
    }
}

// Execution cannot run past the end of the statement
static int terminates(const Stmt *s) {
    switch (s->kind) {
        case ST_RETURN:
            return 1;
        case ST_BLOCK:
            return s->count > 0 && terminates(s->body[s->count - 1]);
        case ST_IF:
            return s->otherwise && terminates(s->then) && terminates(s->otherwise);
        case ST_WHILE:
            return s->cond->is_const && s->cond->value && !breaks(s->then);
        case ST_FOR:
            return (!s->cond || (s->cond->is_const && s->cond->value)) && !breaks(s->then);
        case ST_SWITCH:
            if (!s->fallback || !terminates(s->fallback)) {
                return 0;
            }
            for (int i = 0; i < s->case_count; i++) {
                if (!terminates(s->cases[i].body)) {
                    return 0;
                }
            }
            return 1;
        case ST_EXPR:
            return s->value->kind == EX_CALL && s->value->builtin == BUILTIN_PANIC;
        default:
            return 0;
    }
}

static void check_function(Checker *c, Decl *d) {
    Type *t = d->symbol->type;
    c->fn = d;
    c->function_start = c->local_count = 0;
    c->loops = 0;
    for (int i = 0; i < d->param_count; i++) {
        Symbol *s = allocate(sizeof(Symbol));
        s->kind = SYM_LOCAL;
        s->name = d->params[i].name;
        s->type = t->params[i];
        s->line = d->params[i].line;
        s->module = c->m;
        d->params[i].symbol = s;
        declare_local(c, s);
    }
    check_block(c, d->body);
    if (t->base->kind != TY_VOID && !terminates(d->body)) {
        error(c, d->body->end_line, "%s has to return %s at its end", d->name, type_name(t->base));
    }
}

static void check_const(Checker *c, Decl *d) {
    if (d->state == 2) {
        return;
    }
    if (d->state == 1) {
        error(c, d->line, "the constant %s depends on itself", d->name);
    }
    d->state = 1;
    Module *saved = c->m;
    int saved_locals = c->local_count;
    c->m = d->module;
    c->local_count = 0;
    Symbol *s = d->symbol;
    check_expr(c, d->value);
    if (d->type_expr) {
        s->type = value_type(c, d->type_expr);
        coerce(c, &d->value, s->type);
    } else {
        s->type = d->value->type;
    }
    if (d->value->kind == EX_STRING) {
        s->string = d->value;
    } else if (!d->value->is_const) {
        error(c, d->line, "the value of a constant must be known when compiling");
    }
    s->value = d->value->value;
    c->m = saved;
    c->local_count = saved_locals;
    d->state = 2;
}

// Data can only start out holding what is known before the program runs
static int static_value(const Expr *e) {
    if (e->is_const) {
        return 1;
    }
    switch (e->kind) {
        case EX_NULL:
        case EX_STRING:
            return 1;
        case EX_NAME:
            return e->symbol->kind == SYM_FN || e->symbol->kind == SYM_CONST;
        case EX_UNARY:
            return e->op == TOK_AMP && e->left->kind == EX_NAME && e->left->symbol->kind == SYM_GLOBAL;
        case EX_CONVERT:
            return e->left->kind == EX_NAME && e->left->symbol->kind == SYM_GLOBAL && e->type->kind == TY_SLICE;
        case EX_CAST:
            return static_value(e->left) && e->left->type->kind != TY_INT;
        case EX_STRUCT:
        case EX_ARRAY:
            for (int i = 0; i < e->arg_count; i++) {
                if (!static_value(e->args[i])) {
                    return 0;
                }
            }
            return 1;
        default:
            return 0;
    }
}

static void declare(Checker *c, Module *m, Decl *d) {
    Symbol *s = allocate(sizeof(Symbol));
    s->name = d->name;
    s->decl = d;
    s->module = m;
    s->line = d->line;
    d->symbol = s;
    switch (d->kind) {
        case DECL_IMPORT:
            s->kind = SYM_MODULE;
            s->module = d->imported;
            break;
        case DECL_CONST:
            s->kind = SYM_CONST;
            break;
        case DECL_VAR:
            s->kind = SYM_GLOBAL;
            break;
        case DECL_FN:
            s->kind = SYM_FN;
            break;
        case DECL_STRUCT:
        case DECL_ENUM:
            s->kind = SYM_TYPE;
            s->type = allocate(sizeof(Type));
            s->type->kind = d->kind == DECL_STRUCT ? TY_STRUCT : TY_ENUM;
            s->type->name = d->name;
            s->type->decl = d;
            s->type->size = s->type->align = 4;
            break;
    }
    if (find_universe(d->name) || strcmp(d->name, "new") == 0 || strcmp(d->name, "sizeof") == 0) {
        error(c, d->line, "%s is predeclared", d->name);
    }
    Symbol *old = find(m->symbols, m->symbol_count, d->name);
    if (old) {
        error(c, d->line, "%s is already declared at line %d", d->name, old->line);
    }
    grow((void **)&m->symbols, m->symbol_count + 1, sizeof(Symbol *));
    m->symbols[m->symbol_count++] = s;
}

static void check_types(Checker *c, Decl *d) {
    if (d->kind == DECL_STRUCT) {
        layout(c, d->symbol->type, d->line);
    } else if (d->kind == DECL_ENUM) {
        int64_t next = 0;
        for (int i = 0; i < d->member_count; i++) {
            Member *member = &d->members[i];
            for (int k = 0; k < i; k++) {
                if (strcmp(d->members[k].name, member->name) == 0) {
                    error(c, member->line, "%s is declared twice", member->name);
                }
            }
            if (member->value) {
                next = constant_int(c, member->value, "the value of an enum");
            }
            if (next != (int32_t)next) {
                error(c, member->line, "%lld does not fit in an enum", (long long)next);
            }
            member->number = next++;
        }
    }
}

static void check_signature(Checker *c, Decl *d) {
    Type *t = new_type(TY_FN, d->type_expr ? value_type(c, d->type_expr) : &t_void);
    t->is_interrupt = d->is_interrupt;
    t->is_extern = d->is_extern;
    t->param_count = d->param_count;
    t->params = allocate((size_t)d->param_count * sizeof(Type *));
    for (int i = 0; i < d->param_count; i++) {
        for (int k = 0; k < i; k++) {
            if (strcmp(d->params[k].name, d->params[i].name) == 0) {
                error(c, d->params[i].line, "parameter %s is declared twice", d->params[i].name);
            }
        }
        t->params[i] = value_type(c, d->params[i].type_expr);
    }
    if (d->is_interrupt && (d->param_count > 1 || (d->param_count == 1 && t->params[0]->kind != TY_POINTER) ||
                            d->type_expr)) {
        error(c, d->line, "%s", "an interrupt function returns nothing and takes nothing or a pointer to the saved registers");
    }
    d->symbol->type = t;
}

static void check_global(Checker *c, Decl *d) {
    Symbol *s = d->symbol;
    if (d->value) {
        check_expr(c, d->value);
    }
    if (d->type_expr) {
        s->type = value_type(c, d->type_expr);
        if (d->value) {
            coerce(c, &d->value, s->type);
        }
    } else {
        s->type = infer(c, &d->value, d->line);
    }
    if (d->value && !static_value(d->value)) {
        error(c, d->line, "a global variable needs a value known before the program runs");
    }
}

static void check_main(Checker *c, Module *m) {
    Symbol *s = find(m->symbols, m->symbol_count, "main");
    c->m = m;
    if (!s || s->kind != SYM_FN) {
        error(c, 1, "the program has no main function");
    }
    Type *t = s->type;
    int args = t->param_count == 1 && t->params[0]->kind == TY_SLICE && t->params[0]->base->kind == TY_SLICE &&
               same_type(t->params[0]->base->base, &t_u8);
    if (s->decl->is_extern || s->decl->is_interrupt || (t->param_count > 0 && !args) ||
        (t->base->kind != TY_VOID && !same_type(t->base, &t_i32))) {
        error(c, s->line, "main takes nothing or args: [][]u8, and returns nothing or an int");
    }
    c->program->main = s;
}

void check_program(Program *program) {
    Checker c = { .program = program };

    for (int i = 0; i < program->module_count; i++) {
        c.m = program->modules[i];
        for (int k = 0; k < c.m->decl_count; k++) {
            declare(&c, c.m, c.m->decls[k]);
        }
    }
    for (int i = 0; i < program->module_count; i++) {
        c.m = program->modules[i];
        for (int k = 0; k < c.m->decl_count; k++) {
            check_types(&c, c.m->decls[k]);
        }
    }
    for (int i = 0; i < program->module_count; i++) {
        c.m = program->modules[i];
        for (int k = 0; k < c.m->decl_count; k++) {
            Decl *d = c.m->decls[k];
            if (d->kind == DECL_CONST) {
                check_const(&c, d);
            } else if (d->kind == DECL_FN) {
                check_signature(&c, d);
            }
        }
    }
    for (int i = 0; i < program->module_count; i++) {
        c.m = program->modules[i];
        for (int k = 0; k < c.m->decl_count; k++) {
            if (c.m->decls[k]->kind == DECL_VAR) {
                check_global(&c, c.m->decls[k]);
            }
        }
    }
    for (int i = 0; i < program->module_count; i++) {
        c.m = program->modules[i];
        for (int k = 0; k < c.m->decl_count; k++) {
            Decl *d = c.m->decls[k];
            if (d->kind == DECL_FN && d->body) {
                check_function(&c, d);
            }
        }
    }
    check_main(&c, program->modules[0]);
}
