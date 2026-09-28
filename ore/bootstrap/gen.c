#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ore.h"

// Scalars end up in R0, slices in R0 (pointer) and R5 (length), structs and arrays as their address in
// R0. R6 and R7 are scratch; every variable lives in the frame.

typedef struct {
    char *bytes;
    size_t size;
    size_t capacity;
} Text;

typedef struct {
    const char *bytes;
    int size;
    const char *label;
} PooledString;

typedef struct {
    Program *program;
    Module *m;
    Decl *fn;
    PooledString *pooled;   // every string emitted, so that equal ones share their bytes
    int pooled_count;
    int pooled_capacity;
    Text text;              // code
    Text data;              // global variables
    Text pool;              // strings and the arrays of slice literals
    Text body;              // the function being generated, which needs its frame size first
    int labels;
    int strings;
    int frame;              // bytes of locals and temporaries below BP
    int break_label;
    int continue_label;
    const Module *loc_module;
    int loc_line;
} Gen;

static void gen_expr(Gen *g, Expr *e);
static void gen_addr(Gen *g, Expr *e);
static void gen_block(Gen *g, Stmt *s);

static void append(Text *t, const char *format, ...) {
    va_list args;
    for (;;) {
        size_t room = t->capacity - t->size;
        va_start(args, format);
        int n = vsnprintf(t->bytes ? t->bytes + t->size : NULL, room, format, args);
        va_end(args);
        if (n >= 0 && (size_t)n < room) {
            t->size += (size_t)n;
            return;
        }
        t->capacity = t->capacity ? t->capacity * 2 : 4096;
        if (n >= 0 && t->capacity < t->size + (size_t)n + 1) {
            t->capacity = t->size + (size_t)n + 1;
        }
        grow((void **)&t->bytes, (int)t->capacity, 1);
    }
}

#define emit(g, ...) append(&(g)->body, __VA_ARGS__)

static int label(Gen *g) {
    return ++g->labels;
}

static int is_aggregate(const Type *t) {
    return t->kind == TY_STRUCT || t->kind == TY_ARRAY;
}

static int words(int size) {
    return (size + 3) / 4 * 4;
}

static int alloc_frame(Gen *g, int size) {
    g->frame += words(size > 0 ? size : 1);
    return -g->frame;
}

static const char *symbol_name(const Symbol *s) {
    if (s->decl && s->decl->is_extern) {
        return s->name;
    }
    size_t size = strlen(s->module->name) + strlen(s->name) + 2;
    char *name = allocate(size);
    snprintf(name, size, "%s.%s", s->module->name, s->name);
    return name;
}

// Writes text as an assembler string, escaping what vmasm would read differently
static void quoted(Text *t, const char *text, size_t length) {
    append(t, "\"");
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\') {
            append(t, "\\%c", c);
        } else if (c == '\n' || c == '\t') {
            append(t, c == '\n' ? "\\n" : "\\t");
        } else if (c >= 32 && c < 127) {
            append(t, "%c", c);
        } else {
            append(t, "\\x%02X", c);
        }
    }
    append(t, "\"");
}

static void loc(Gen *g, int line) {
    if (g->program->no_loc || (g->loc_module == g->m && g->loc_line == line)) {
        return;
    }
    g->loc_module = g->m;
    g->loc_line = line;
    const char *text = "";
    size_t length = 0;
    if (line >= 1 && line <= g->m->line_count) {
        text = g->m->lines[line - 1];
        length = strcspn(text, "\r\n");
    }
    emit(g, "    .loc ");
    quoted(&g->body, g->m->path, strlen(g->m->path));
    emit(g, ", %d, ", line);
    quoted(&g->body, text, length);
    emit(g, "\n");
}

// Small integers are kept zero- or sign-extended to 32 bits
static void normalize(Gen *g, const Type *t) {
    if (t->kind != TY_INT || t->size == 4) {
        return;
    }
    if (t->is_signed) {
        emit(g, "    SHL R0, #%d\n    SAR R0, #%d\n", 32 - 8 * t->size, 32 - 8 * t->size);
    } else {
        emit(g, "    AND R0, #%d\n", t->size == 1 ? 0xFF : 0xFFFF);
    }
}

// Loads the value of type t from the address place into R0 (and R5); place is a register, BP-8 or a label
static void load_from(Gen *g, const Type *t, const char *place) {
    if (t->kind == TY_SLICE) {
        emit(g, "    LOAD R5, [%s+4]\n    LOAD R0, [%s]\n", place, place);
    } else if (is_aggregate(t)) {
        if (strcmp(place, "R0") != 0) {
            emit(g, place[0] == 'R' || place[0] == 'B' ? "    LEA R0, [%s]\n" : "    LOAD R0, %s\n", place);
        }
    } else if (t->size == 1) {
        emit(g, "    LOADB R0, [%s]\n", place);
        normalize(g, t);
    } else if (t->size == 2) {
        emit(g, "    LOADW R0, [%s]\n", place);
        normalize(g, t);
    } else {
        emit(g, "    LOAD R0, [%s]\n", place);
    }
}

// Stores the value in R0 (and R5), or the aggregate R0 points at, to place
static void store_to(Gen *g, const Type *t, const char *place) {
    if (t->kind == TY_SLICE) {
        emit(g, "    STORE R0, [%s]\n    STORE R5, [%s+4]\n", place, place);
    } else if (is_aggregate(t)) {
        if (t->size > 0) {
            if (strcmp(place, "R6") != 0) {
                emit(g, place[0] == 'B' ? "    LEA R6, [%s]\n" : "    LOAD R6, %s\n", place);
            }
            emit(g, "    MEMCPY R6, R0, #%d\n", t->size);
        }
    } else {
        emit(g, "    %s R0, [%s]\n", t->size == 1 ? "STOREB" : t->size == 2 ? "STOREW" : "STORE", place);
    }
}

static void store(Gen *g, const Type *t) {
    store_to(g, t, "R6");
}

static const char *symbol_name(const Symbol *s);

// A variable that instructions can address directly, as BP-8 or its label
static const char *place(const Expr *e) {
    static char text[32];
    if (e->kind != EX_NAME || (e->symbol->kind != SYM_LOCAL && e->symbol->kind != SYM_GLOBAL)) {
        return NULL;
    }
    if (e->symbol->kind == SYM_GLOBAL) {
        return symbol_name(e->symbol);
    }
    snprintf(text, sizeof(text), "BP%+d", e->symbol->offset);
    return text;
}

static void zero(Gen *g, const Type *t, int offset) {
    if (t->size == 0) {
        return;
    }
    if (is_scalar(t) && t->size == 4) {
        emit(g, "    LOAD R0, #0\n    STORE R0, [BP%+d]\n", offset);
        return;
    }
    emit(g, "    LEA R6, [BP%+d]\n    LOAD R7, #0\n    MEMSET R6, R7, #%d\n", offset, t->size);
}

static void check_null(Gen *g, const char *reg) {
    int ok = label(g);
    emit(g, "    CMP %s, #0\n    JNZ .L%d\n    CALL rt.null_error\n.L%d:\n", reg, ok, ok);
}

// Multiplies R0 by the size of an element
static void scale(Gen *g, int size, const char *reg) {
    if (size == 1) {
        return;
    }
    if ((size & (size - 1)) == 0) {
        int shift = 0;
        while ((1 << shift) < size) {
            shift++;
        }
        emit(g, "    SHL %s, #%d\n", reg, shift);
    } else {
        emit(g, "    MUL %s, #%d\n", reg, size);
    }
}

// Labels of data without a name of its own; digits after the dot keep them apart from Ore names
static const char *pool_label(Gen *g) {
    size_t size = strlen(g->m->name) + 16;
    char *name = allocate(size);
    snprintf(name, size, "%s.%d", g->m->name, ++g->strings);
    return name;
}

static const char *string_label(Gen *g, const char *bytes, int size) {
    for (int i = 0; i < g->pooled_count; i++) {
        if (g->pooled[i].size == size && memcmp(g->pooled[i].bytes, bytes, (size_t)size) == 0) {
            return g->pooled[i].label;
        }
    }
    const char *name = pool_label(g);
    append(&g->pool, "%s:\n    .asciiz ", name);
    quoted(&g->pool, bytes, (size_t)size);
    append(&g->pool, "\n");
    extend((void **)&g->pooled, &g->pooled_capacity, g->pooled_count, sizeof(PooledString));
    g->pooled[g->pooled_count++] = (PooledString){ bytes, size, name };
    return name;
}

// A string constant keeps one copy of its bytes however often it is used
static const char *string_of(Gen *g, Expr *e) {
    if (e->kind != EX_NAME) {
        return string_label(g, e->bytes, e->size);
    }
    Symbol *s = e->symbol;
    if (!s->label) {
        s->label = string_label(g, s->string->bytes, s->string->size);
    }
    return s->label;
}

static int string_size(const Expr *e) {
    return e->kind == EX_NAME ? e->symbol->string->size : e->size;
}

static void gen_string(Gen *g, Expr *e) {
    emit(g, "    LOAD R0, %s\n    LOAD R5, #%d\n", string_of(g, e), string_size(e));
}

static int is_signed(const Type *t) {
    return t->kind == TY_ENUM || t->kind == TY_UNTYPED || (t->kind == TY_INT && t->is_signed);
}

// The condition code of a comparison, or of its opposite
static const char *condition(TokenKind op, const Type *t, int inverse) {
    static const TokenKind order[] = { TOK_EQ, TOK_NE, TOK_LT, TOK_GE, TOK_LE, TOK_GT };
    static const char *const signed_codes[] = { "Z", "NZ", "L", "GE", "LE", "G" };
    static const char *const unsigned_codes[] = { "Z", "NZ", "B", "AE", "BE", "A" };
    int i = 0;
    while (order[i] != op) {
        i++;
    }
    i ^= inverse;
    return is_signed(t) ? signed_codes[i] : unsigned_codes[i];
}

static int is_comparison(const Expr *e) {
    return e->kind == EX_BINARY && e->op >= TOK_EQ && e->op <= TOK_GE;
}

// Leaves the left operand in R0 and puts the right one where the instruction can take it
static const char *operands(Gen *g, Expr *e) {
    if (e->right->is_const && is_scalar(e->right->type)) {
        gen_expr(g, e->left);
        char *immediate = allocate(16);
        snprintf(immediate, 16, "#%d", (int)(int32_t)e->right->value);
        return immediate;
    }
    // Memory operands are read as 32 bits
    if (place(e->right) && is_scalar(e->right->type) && e->right->type->size == 4) {
        gen_expr(g, e->left);
        size_t size = strlen(place(e->right)) + 3;
        char *memory = allocate(size);
        snprintf(memory, size, "[%s]", place(e->right));
        return memory;
    }
    gen_expr(g, e->left);
    emit(g, "    PUSH R0\n");
    gen_expr(g, e->right);
    emit(g, "    MOVE R6, R0\n    POP R0\n");
    return "R6";
}

static void arithmetic(Gen *g, TokenKind op, const Type *t, const char *right) {
    const char *instruction = "ADD";
    switch (op) {
        case TOK_PLUS:
            instruction = "ADD";
            break;
        case TOK_MINUS:
            instruction = "SUB";
            break;
        case TOK_STAR:
            instruction = "MUL";
            break;
        case TOK_SLASH:
            instruction = is_signed(t) ? "IDIV" : "DIV";
            break;
        case TOK_PERCENT:
            instruction = is_signed(t) ? "IMOD" : "MOD";
            break;
        case TOK_AMP:
            instruction = "AND";
            break;
        case TOK_PIPE:
            instruction = "OR";
            break;
        case TOK_CARET:
            instruction = "XOR";
            break;
        case TOK_SHL:
            instruction = "SHL";
            break;
        case TOK_SHR:
            instruction = is_signed(t) ? "SAR" : "SHR";
            break;
        default:
            break;
    }
    emit(g, "    %s R0, %s\n", instruction, right);
    normalize(g, t);
}

static void gen_binary(Gen *g, Expr *e) {
    Type *left = e->left->type;

    if (e->op == TOK_ANDAND || e->op == TOK_OROR) {
        int end = label(g);
        gen_expr(g, e->left);
        emit(g, "    CMP R0, #0\n    %s .L%d\n", e->op == TOK_ANDAND ? "JZ" : "JNZ", end);
        gen_expr(g, e->right);
        emit(g, ".L%d:\n", end);
        return;
    }
    if (left->kind == TY_POINTER && !is_comparison(e)) {
        int size = left->base->size;
        gen_expr(g, e->left);
        emit(g, "    PUSH R0\n");
        gen_expr(g, e->right);
        if (e->right->type->kind == TY_POINTER) {
            emit(g, "    MOVE R6, R0\n    POP R0\n    SUB R0, R6\n");
            if (size > 1) {
                emit(g, "    IDIV R0, #%d\n", size);
            }
            return;
        }
        scale(g, size, "R0");
        emit(g, "    MOVE R6, R0\n    POP R0\n    %s R0, R6\n", e->op == TOK_PLUS ? "ADD" : "SUB");
        return;
    }

    const char *right = operands(g, e);
    if (is_comparison(e)) {
        emit(g, "    CMP R0, %s\n    SET%s R0\n", right, condition(e->op, left, 0));
        return;
    }
    arithmetic(g, e->op, e->type, right);
}

// Jumps to target unless the condition holds
static void gen_branch(Gen *g, Expr *e, int target) {
    if (is_comparison(e) && is_scalar(e->left->type)) {
        const char *right = operands(g, e);
        emit(g, "    CMP R0, %s\n    J%s .L%d\n", right, condition(e->op, e->left->type, 1), target);
        return;
    }
    gen_expr(g, e);
    emit(g, "    CMP R0, #0\n    JZ .L%d\n", target);
}

// Evaluates the arguments from left to right into their places below SP; returns the bytes they take
static void gen_argument(Gen *g, Expr *arg, int offset) {
    Type *t = arg->type;
    gen_expr(g, arg);
    if (t->kind == TY_SLICE) {
        emit(g, "    STORE R0, [SP+%d]\n    STORE R5, [SP+%d]\n", offset, offset + 4);
    } else if (is_aggregate(t)) {
        if (t->size > 0) {
            emit(g, "    LEA R6, [SP+%d]\n    MEMCPY R6, R0, #%d\n", offset, t->size);
        }
    } else {
        emit(g, "    STORE R0, [SP+%d]\n", offset);
    }
}

static void gen_call(Gen *g, Expr *e) {
    Type *fn = e->left->type;
    Type *result = fn->base;
    int hidden = is_aggregate(result);
    int size = hidden ? 4 : 0;
    int direct = e->left->kind == EX_NAME && e->left->symbol->kind == SYM_FN;
    int temp = 0;

    for (int i = 0; i < fn->param_count; i++) {
        size += words(fn->params[i]->size);
    }
    if (!direct) {
        gen_expr(g, e->left);
        emit(g, "    PUSH R0\n");
    }
    if (size) {
        emit(g, "    SUB SP, #%d\n", size);
    }
    int offset = 0;
    if (hidden) {
        temp = alloc_frame(g, result->size);
        emit(g, "    LEA R0, [BP%+d]\n    STORE R0, [SP]\n", temp);
        offset = 4;
    }
    for (int i = 0; i < e->arg_count; i++) {
        gen_argument(g, e->args[i], offset);
        offset += words(fn->params[i]->size);
    }
    if (direct) {
        emit(g, "    CALL %s\n", symbol_name(e->left->symbol));
    } else {
        emit(g, "    LOAD R6, [SP+%d]\n", size);
        check_null(g, "R6");
        emit(g, "    CALL R6\n");
        size += 4;
    }
    if (size) {
        emit(g, "    ADD SP, #%d\n", size);
    }
    if (hidden) {
        emit(g, "    LEA R0, [BP%+d]\n", temp);
    }
}

static void gen_print(Gen *g, Expr *e) {
    for (int i = 0; i < e->arg_count; i++) {
        Type *t = e->args[i]->type;
        gen_expr(g, e->args[i]);
        if (t->kind == TY_SLICE) {
            emit(g, "    MOVE R6, R5\n    MOVE R5, R0\n    LOAD R0, #1\n    SYSCALL #13\n");
        } else if (t->kind == TY_BOOL) {
            int done = label(g);
            emit(g, "    LOAD R6, rt.false\n    CMP R0, #0\n    JZ .L%d\n    LOAD R6, rt.true\n.L%d:\n"
                    "    MOVE R0, R6\n    SYSCALL #2\n", done, done);
        } else if (t->kind == TY_POINTER || t->kind == TY_FN) {
            emit(g, "    SYSCALL #5\n");
        } else if (t->kind == TY_INT && !t->is_signed) {
            emit(g, "    LOAD R5, #10\n    SYSCALL #6\n");
        } else {
            emit(g, "    SYSCALL #1\n");
        }
    }
}

static void gen_builtin(Gen *g, Expr *e) {
    switch (e->builtin) {
        case BUILTIN_PRINT:
            gen_print(g, e);
            break;
        case BUILTIN_PANIC:
            gen_expr(g, e->args[0]);
            emit(g, "    PUSH R5\n    PUSH R0\n    CALL rt.panic\n");
            break;
        case BUILTIN_ASSERT: {
            int ok = label(g);
            gen_expr(g, e->args[0]);
            emit(g, "    CMP R0, #0\n    JNZ .L%d\n    CALL rt.assert_error\n.L%d:\n", ok, ok);
            break;
        }
        case BUILTIN_FREE:
            gen_expr(g, e->args[0]);
            emit(g, "    PUSH R0\n    CALL rt.free\n    ADD SP, #4\n");
            break;
        case BUILTIN_SYSCALL: {
            static const char *const registers[] = { "R0", "R5", "R6" };
            int temp = alloc_frame(g, 8);
            for (int i = 1; i < e->arg_count; i++) {
                gen_expr(g, e->args[i]);
                emit(g, "    PUSH R0\n");
            }
            for (int i = e->arg_count - 1; i >= 1; i--) {
                emit(g, "    POP %s\n", registers[i - 1]);
            }
            emit(g, "    SYSCALL #%d\n    STORE R0, [BP%+d]\n    STORE R5, [BP%+d]\n    LEA R0, [BP%+d]\n",
                 (int)e->args[0]->value, temp, temp + 4, temp);
            break;
        }
    }
}

static void gen_index(Gen *g, Expr *e) {
    Type *t = e->left->type;
    int size = t->base->size;
    int ok = label(g);

    gen_expr(g, e->left);
    if (t->kind == TY_POINTER) {
        check_null(g, "R0");
    }
    emit(g, "    PUSH R0\n");
    if (t->kind == TY_SLICE) {
        emit(g, "    PUSH R5\n");
    }
    gen_expr(g, e->right);
    if (t->kind == TY_SLICE) {
        emit(g, "    POP R7\n    POP R6\n    CMP R0, R7\n    JB .L%d\n    PUSH R7\n    PUSH R0\n"
                "    CALL rt.index_error\n.L%d:\n", ok, ok);
    } else {
        emit(g, "    POP R6\n");
        if (t->kind == TY_ARRAY && !e->right->is_const) {
            emit(g, "    CMP R0, #%d\n    JB .L%d\n    PUSH #%d\n    PUSH R0\n    CALL rt.index_error\n.L%d:\n",
                 t->length, ok, t->length, ok);
        }
    }
    scale(g, size, "R0");
    emit(g, "    ADD R0, R6\n");
}

// Leaves the pointer in R0 and the length in R5
static void gen_slice(Gen *g, Expr *e) {
    Type *t = e->left->type;
    int fail = label(g), ok = label(g);

    if (t->kind == TY_ARRAY) {
        gen_addr(g, e->left);
        emit(g, "    PUSH R0\n    PUSH #%d\n", t->length);
    } else {
        gen_expr(g, e->left);
        if (t->kind == TY_POINTER) {
            check_null(g, "R0");
            emit(g, "    PUSH R0\n    PUSH #-1\n");
        } else {
            emit(g, "    PUSH R0\n    PUSH R5\n");
        }
    }
    if (e->right) {
        gen_expr(g, e->right);
        emit(g, "    PUSH R0\n");
    } else {
        emit(g, "    PUSH #0\n");
    }
    if (e->third) {
        gen_expr(g, e->third);
    } else {
        emit(g, "    LOAD R0, [SP+4]\n");
    }
    emit(g, "    POP R7\n    POP R6\n");
    if (t->kind != TY_POINTER) {
        emit(g, "    CMP R0, R6\n    JA .L%d\n", fail);
    }
    emit(g, "    CMP R7, R0\n    JBE .L%d\n.L%d:\n    PUSH R6\n    PUSH R0\n    PUSH R7\n    CALL rt.slice_error\n.L%d:\n",
         ok, fail, ok);
    emit(g, "    MOVE R5, R0\n    SUB R5, R7\n");
    scale(g, t->base->size, "R7");
    emit(g, "    POP R0\n    ADD R0, R7\n");
}

// Builds a struct, array or slice literal in a temporary of the frame
static void gen_literal(Gen *g, Expr *e) {
    Type *t = e->type;
    Type *storage = t;
    if (t->kind == TY_SLICE) {
        storage = allocate(sizeof(Type));
        *storage = (Type){ .kind = TY_ARRAY, .base = t->base, .length = e->arg_count,
                           .size = e->arg_count * t->base->size, .align = t->base->align };
    }
    int temp = alloc_frame(g, storage->size);
    zero(g, storage, temp);
    for (int i = 0; i < e->arg_count; i++) {
        Type *field = t->base;
        int offset = e->kind == EX_STRUCT ? 0 : i * t->base->size;
        if (e->kind == EX_STRUCT) {
            for (int k = 0; k < t->field_count; k++) {
                if (strcmp(t->fields[k].name, e->fields[i]) == 0) {
                    field = t->fields[k].type;
                    offset = t->fields[k].offset;
                }
            }
        }
        gen_expr(g, e->args[i]);
        emit(g, "    LEA R6, [BP%+d]\n", temp + offset);
        store(g, field);
    }
    emit(g, "    LEA R0, [BP%+d]\n", temp);
    if (t->kind == TY_SLICE) {
        emit(g, "    LOAD R5, #%d\n", e->arg_count);
    }
}

static void gen_new(Gen *g, Expr *e) {
    int size = e->named->size;
    if (!e->left) {
        emit(g, "    PUSH #%d\n    CALL rt.alloc\n    ADD SP, #4\n", size);
        return;
    }
    gen_expr(g, e->left);
    emit(g, "    PUSH R0\n");
    scale(g, size, "R0");
    emit(g, "    PUSH R0\n    CALL rt.alloc\n    ADD SP, #4\n    POP R5\n");
}

static void gen_addr(Gen *g, Expr *e) {
    switch (e->kind) {
        case EX_NAME:
            if (e->symbol->kind == SYM_LOCAL) {
                emit(g, "    LEA R0, [BP%+d]\n", e->symbol->offset);
            } else {
                emit(g, "    LOAD R0, %s\n", symbol_name(e->symbol));
            }
            return;
        case EX_UNARY:
            gen_expr(g, e->left);
            check_null(g, "R0");
            return;
        case EX_FIELD:
            gen_expr(g, e->left);
            if (e->left->type->kind == TY_POINTER) {
                check_null(g, "R0");
            }
            if (e->field->offset) {
                emit(g, "    ADD R0, #%d\n", e->field->offset);
            }
            return;
        case EX_INDEX:
            gen_index(g, e);
            return;
        default:
            // Aggregates evaluate to their address
            gen_expr(g, e);
            return;
    }
}

static void gen_expr(Gen *g, Expr *e) {
    Type *t = e->type;

    if (e->is_const && is_scalar(t)) {
        emit(g, "    LOAD R0, #%d\n", (int)(int32_t)e->value);
        return;
    }
    switch (e->kind) {
        case EX_NULL:
            emit(g, "    LOAD R0, #0\n");
            return;
        case EX_STRING:
            gen_string(g, e);
            return;
        case EX_NAME:
            if (e->symbol->kind == SYM_CONST) {
                gen_string(g, e);
            } else if (e->symbol->kind == SYM_FN) {
                emit(g, "    LOAD R0, %s\n", symbol_name(e->symbol));
            } else {
                load_from(g, t, place(e));
            }
            return;
        case EX_CONVERT:
            gen_expr(g, e->left);
            if (e->left->type->kind == TY_ARRAY) {
                emit(g, "    LOAD R5, #%d\n", e->left->type->length);
            }
            return;
        case EX_LEN:
            gen_expr(g, e->left);
            emit(g, "    MOVE R0, R5\n");
            return;
        case EX_PTR:
            gen_expr(g, e->left);
            return;
        case EX_UNARY:
            if (e->op == TOK_AMP) {
                gen_addr(g, e->left);
                return;
            }
            if (e->op == TOK_STAR) {
                gen_addr(g, e);
                load_from(g, t, "R0");
                return;
            }
            gen_expr(g, e->left);
            if (e->op == TOK_BANG) {
                emit(g, "    XOR R0, #1\n");
            } else {
                emit(g, "    %s R0\n", e->op == TOK_MINUS ? "NEG" : "NOT");
                normalize(g, t);
            }
            return;
        case EX_BINARY:
            gen_binary(g, e);
            return;
        case EX_CALL:
            if (e->builtin) {
                gen_builtin(g, e);
            } else {
                gen_call(g, e);
            }
            return;
        case EX_INDEX:
        case EX_FIELD:
            gen_addr(g, e);
            load_from(g, t, "R0");
            return;
        case EX_SLICE:
            gen_slice(g, e);
            return;
        case EX_CAST:
            gen_expr(g, e->left);
            normalize(g, t);
            return;
        case EX_STRUCT:
        case EX_ARRAY:
            gen_literal(g, e);
            return;
        case EX_NEW:
            gen_new(g, e);
            return;
        default:
            fail(g->m, e->line, "cannot generate code for this expression (internal error)");
    }
}

static void gen_assign(Gen *g, Stmt *s) {
    Type *t = s->target->type;
    const char *direct = place(s->target);
    const char *target = "R6";
    if (direct) {
        target = copy_text(direct, strlen(direct));
    } else {
        gen_addr(g, s->target);
        emit(g, "    PUSH R0\n");
    }
    gen_expr(g, s->value);
    if (s->op == TOK_ASSIGN) {
        if (!direct) {
            emit(g, "    POP R6\n");
        }
        store_to(g, t, target);
        return;
    }
    static const TokenKind ops[] = {
        [TOK_ADD_ASSIGN] = TOK_PLUS, [TOK_SUB_ASSIGN] = TOK_MINUS, [TOK_MUL_ASSIGN] = TOK_STAR,
        [TOK_DIV_ASSIGN] = TOK_SLASH, [TOK_MOD_ASSIGN] = TOK_PERCENT, [TOK_AND_ASSIGN] = TOK_AMP,
        [TOK_OR_ASSIGN] = TOK_PIPE, [TOK_XOR_ASSIGN] = TOK_CARET, [TOK_SHL_ASSIGN] = TOK_SHL,
        [TOK_SHR_ASSIGN] = TOK_SHR,
    };
    if (t->kind == TY_POINTER) {
        scale(g, t->base->size, "R0");
    }
    emit(g, "    MOVE R7, R0\n");
    if (!direct) {
        emit(g, "    POP R6\n");
    }
    load_from(g, t, target);
    arithmetic(g, ops[s->op], t, "R7");
    store_to(g, t, target);
}

static void gen_variable(Gen *g, Stmt *s) {
    Symbol *sym = s->symbol;
    if (s->kind == ST_CONST) {
        return;
    }
    sym->offset = alloc_frame(g, sym->type->size);
    if (!s->value) {
        zero(g, sym->type, sym->offset);
        return;
    }
    gen_expr(g, s->value);
    char local[32];
    snprintf(local, sizeof(local), "BP%+d", sym->offset);
    store_to(g, sym->type, local);
}

static void gen_switch(Gen *g, Stmt *s) {
    int end = label(g), fallback = label(g);
    int first = g->labels + 1;
    g->labels += s->case_count;

    gen_expr(g, s->cond);
    for (int i = 0; i < s->case_count; i++) {
        for (int v = 0; v < s->cases[i].value_count; v++) {
            emit(g, "    CMP R0, #%d\n    JZ .L%d\n", (int)(int32_t)s->cases[i].values[v]->value, first + i);
        }
    }
    emit(g, "    JMP .L%d\n", s->fallback ? fallback : end);
    for (int i = 0; i < s->case_count; i++) {
        emit(g, ".L%d:\n", first + i);
        gen_block(g, s->cases[i].body);
        emit(g, "    JMP .L%d\n", end);
    }
    if (s->fallback) {
        emit(g, ".L%d:\n", fallback);
        gen_block(g, s->fallback);
    }
    emit(g, ".L%d:\n", end);
}

static void gen_stmt(Gen *g, Stmt *s);

static void gen_loop(Gen *g, Stmt *s) {
    int top = label(g), next = label(g), end = label(g);
    int saved_break = g->break_label, saved_continue = g->continue_label;

    if (s->init) {
        gen_stmt(g, s->init);
    }
    emit(g, ".L%d:\n", top);
    if (s->cond) {
        loc(g, s->line);
        gen_branch(g, s->cond, end);
    }
    g->break_label = end;
    g->continue_label = next;
    gen_block(g, s->then);
    g->break_label = saved_break;
    g->continue_label = saved_continue;
    emit(g, ".L%d:\n", next);
    if (s->step) {
        gen_stmt(g, s->step);
    }
    emit(g, "    JMP .L%d\n.L%d:\n", top, end);
}

static void gen_stmt(Gen *g, Stmt *s) {
    if (s->kind != ST_BLOCK) {
        loc(g, s->line);
    }
    switch (s->kind) {
        case ST_BLOCK:
            gen_block(g, s);
            break;
        case ST_VAR:
        case ST_CONST:
            gen_variable(g, s);
            break;
        case ST_ASSIGN:
            gen_assign(g, s);
            break;
        case ST_EXPR:
            gen_expr(g, s->value);
            break;
        case ST_IF: {
            int otherwise = label(g), end = label(g);
            gen_branch(g, s->cond, otherwise);
            gen_block(g, s->then);
            if (s->otherwise) {
                emit(g, "    JMP .L%d\n", end);
            }
            emit(g, ".L%d:\n", otherwise);
            if (s->otherwise) {
                gen_stmt(g, s->otherwise);
                emit(g, ".L%d:\n", end);
            }
            break;
        }
        case ST_WHILE:
        case ST_FOR:
            gen_loop(g, s);
            break;
        case ST_SWITCH:
            gen_switch(g, s);
            break;
        case ST_BREAK:
            emit(g, "    JMP .L%d\n", g->break_label);
            break;
        case ST_CONTINUE:
            emit(g, "    JMP .L%d\n", g->continue_label);
            break;
        case ST_RETURN:
            if (s->value) {
                gen_expr(g, s->value);
                if (is_aggregate(s->value->type) && s->value->type->size > 0) {
                    emit(g, "    LOAD R6, [BP+8]\n    MEMCPY R6, R0, #%d\n    MOVE R0, R6\n", s->value->type->size);
                } else if (is_aggregate(s->value->type)) {
                    emit(g, "    LOAD R0, [BP+8]\n");
                }
            }
            emit(g, "    JMP .ret\n");
            break;
    }
}

static void gen_block(Gen *g, Stmt *s) {
    for (int i = 0; i < s->count; i++) {
        gen_stmt(g, s->body[i]);
    }
}

static void gen_function(Gen *g, Decl *d) {
    Type *t = d->symbol->type;
    int offset = is_aggregate(t->base) ? 12 : 8;

    g->fn = d;
    g->frame = 0;
    g->body.size = 0;
    for (int i = 0; i < d->param_count; i++) {
        d->params[i].symbol->offset = offset;
        offset += words(t->params[i]->size);
    }
    g->loc_module = NULL;
    loc(g, d->line);
    size_t start = g->body.size;
    // The parameter of an interrupt function points at the registers saved above the frame
    if (d->is_interrupt && d->param_count == 1) {
        d->params[0].symbol->offset = alloc_frame(g, 4);
        emit(g, "    LEA R0, [BP+4]\n    STORE R0, [BP%+d]\n", d->params[0].symbol->offset);
    }
    gen_block(g, d->body);
    loc(g, d->body->end_line);

    // ENTER needs the size of the frame, known only after the body
    append(&g->text, "\n%.*s%s:\n    ENTER #%d\n%.*s.ret:\n    LEAVE\n    %s\n", (int)start, g->body.bytes,
           symbol_name(d->symbol), g->frame, (int)(g->body.size - start), g->body.bytes + start,
           d->is_interrupt ? "IRET" : "RET");
}

// Emits the initial value of data of type t
static void gen_data(Gen *g, Text *out, const Type *t, Expr *e) {
    if (!e) {
        if (t->size > 0) {
            append(out, "    .space %d\n", t->size);
        }
        return;
    }
    if (e->kind == EX_CONVERT && e->type->kind == TY_SLICE) {
        append(out, "    .dword %s, %d\n", symbol_name(e->left->symbol), e->left->type->length);
        return;
    }
    if (t->kind == TY_SLICE && e->kind == EX_ARRAY) {
        Type array = { .kind = TY_ARRAY, .base = t->base, .length = e->arg_count,
                       .size = e->arg_count * t->base->size, .align = t->base->align };
        Text inner = { 0 };
        const char *name = pool_label(g);
        append(&inner, "    .align %d\n%s:\n", array.align ? array.align : 1, name);
        gen_data(g, &inner, &array, e);
        append(&g->pool, "%.*s", (int)inner.size, inner.bytes);
        free(inner.bytes);
        append(out, "    .dword %s, %d\n", name, e->arg_count);
        return;
    }
    if (t->kind == TY_SLICE) {
        append(out, "    .dword %s, %d\n", string_of(g, e), string_size(e));
        return;
    }
    if (t->kind == TY_STRUCT || t->kind == TY_ARRAY) {
        int offset = 0;
        for (int i = 0; i < e->arg_count; i++) {
            const Type *element = t->base;
            int at = t->kind == TY_ARRAY ? i * t->base->size : 0;
            if (t->kind == TY_STRUCT) {
                for (int k = 0; k < t->field_count; k++) {
                    if (strcmp(t->fields[k].name, e->fields[i]) == 0) {
                        element = t->fields[k].type;
                        at = t->fields[k].offset;
                    }
                }
            }
            if (at > offset) {
                append(out, "    .space %d\n", at - offset);
            }
            gen_data(g, out, element, e->args[i]);
            offset = at + element->size;
        }
        if (t->size > offset) {
            append(out, "    .space %d\n", t->size - offset);
        }
        return;
    }
    const char *directive = t->size == 1 ? ".byte" : t->size == 2 ? ".word" : ".dword";
    if (e->is_const) {
        append(out, "    %s %d\n", directive, (int)(int32_t)e->value);
    } else if (e->kind == EX_NULL) {
        append(out, "    .dword 0\n");
    } else if (e->kind == EX_CAST) {
        gen_data(g, out, t, e->left);
    } else if (e->kind == EX_UNARY) {
        append(out, "    .dword %s\n", symbol_name(e->left->symbol));
    } else {
        append(out, "    .dword %s\n", symbol_name(e->symbol));
    }
}

// Struct literals in data list their fields in the order written; data needs them in memory order
static void sort_fields(Expr *e) {
    if ((e->kind != EX_STRUCT && e->kind != EX_ARRAY) || !e->type) {
        return;
    }
    for (int i = 0; i < e->arg_count; i++) {
        sort_fields(e->args[i]);
    }
    if (e->kind != EX_STRUCT) {
        return;
    }
    const Type *t = e->type;
    for (int i = 1; i < e->arg_count; i++) {
        for (int k = i; k > 0; k--) {
            int a = -1, b = -1;
            for (int f = 0; f < t->field_count; f++) {
                a = strcmp(t->fields[f].name, e->fields[k - 1]) == 0 ? f : a;
                b = strcmp(t->fields[f].name, e->fields[k]) == 0 ? f : b;
            }
            if (a <= b) {
                break;
            }
            Expr *arg = e->args[k];
            const char *field = e->fields[k];
            e->args[k] = e->args[k - 1];
            e->fields[k] = e->fields[k - 1];
            e->args[k - 1] = arg;
            e->fields[k - 1] = field;
        }
    }
}

static void gen_global(Gen *g, Decl *d) {
    Type *t = d->symbol->type;
    if (d->value) {
        sort_fields(d->value);
    }
    append(&g->data, "    .align %d\n%s:\n", t->align > 0 ? t->align : 1, symbol_name(d->symbol));
    gen_data(g, &g->data, t, d->value);
}

// The runtime starts the program at main, which passes the arguments if the program's main takes them
static void gen_entry(Gen *g) {
    Symbol *main = g->program->main;
    Type *t = main->type;
    g->m = main->decl->module;
    g->body.size = 0;
    g->loc_module = NULL;
    loc(g, main->line);
    append(&g->text, "%.*s", (int)g->body.size, g->body.bytes);
    append(&g->text, "main:\n");
    if (t->param_count) {
        append(&g->text, "    CALL rt.args\n    PUSH R5\n    PUSH R0\n");
    }
    append(&g->text, "    CALL %s\n", symbol_name(main));
    if (t->param_count) {
        append(&g->text, "    ADD SP, #8\n");
    }
    if (t->base->kind == TY_VOID) {
        append(&g->text, "    LOAD R0, #0\n");
    }
    append(&g->text, "    RET\n");
}

typedef struct {
    Decl **items;
    int count;
    int capacity;
} Pending;

// A function or global joins pending the first time something uses it
static void reach(Pending *pending, const Symbol *s) {
    if (s && s->decl && !s->decl->used && (s->kind == SYM_FN || s->kind == SYM_GLOBAL)) {
        s->decl->used = 1;
        extend((void **)&pending->items, &pending->capacity, pending->count, sizeof(Decl *));
        pending->items[pending->count++] = s->decl;
    }
}

static void reach_expr(Pending *pending, const Expr *e) {
    if (!e) {
        return;
    }
    reach(pending, e->symbol);
    reach_expr(pending, e->left);
    reach_expr(pending, e->right);
    reach_expr(pending, e->third);
    for (int i = 0; i < e->arg_count; i++) {
        reach_expr(pending, e->args[i]);
    }
}

static void reach_stmt(Pending *pending, const Stmt *s) {
    if (!s) {
        return;
    }
    for (int i = 0; i < s->count; i++) {
        reach_stmt(pending, s->body[i]);
    }
    reach_expr(pending, s->value);
    reach_expr(pending, s->target);
    reach_expr(pending, s->cond);
    reach_stmt(pending, s->then);
    reach_stmt(pending, s->otherwise);
    reach_stmt(pending, s->init);
    reach_stmt(pending, s->step);
    for (int i = 0; i < s->case_count; i++) {
        reach_stmt(pending, s->cases[i].body);
    }
    reach_stmt(pending, s->fallback);
}

// Assembly names functions and globals as module.name
static void reach_assembly(const Program *program, Pending *pending, const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return;
    }
    char word[256];
    size_t length = 0;
    for (int c = getc(file);; c = getc(file)) {
        if (c != EOF && (isalnum(c) || c == '_' || c == '.')) {
            if (length < sizeof(word) - 1) {
                word[length++] = (char)c;
            }
            continue;
        }
        word[length] = '\0';
        char *dot = strchr(word, '.');
        for (int i = 0; dot && dot > word && i < program->module_count; i++) {
            const Module *m = program->modules[i];
            if (strlen(m->name) == (size_t)(dot - word) && strncmp(m->name, word, (size_t)(dot - word)) == 0) {
                for (int k = 0; k < m->symbol_count; k++) {
                    if (strcmp(m->symbols[k]->name, dot + 1) == 0) {
                        reach(pending, m->symbols[k]);
                    }
                }
            }
        }
        length = 0;
        if (c == EOF) {
            break;
        }
    }
    fclose(file);
}

// Marks what main reaches, directly or through other functions and globals, and what the program's
// assembly names, which is all that gets generated
static void mark_used(Program *program) {
    Pending pending = { 0 };
    reach(&pending, program->main);
    for (int i = 0; i < program->assembly_count; i++) {
        reach_assembly(program, &pending, program->assembly[i]);
    }
    for (int i = 0; i < pending.count; i++) {
        reach_expr(&pending, pending.items[i]->value);
        reach_stmt(&pending, pending.items[i]->body);
    }
    free(pending.items);
}

char *generate(Program *program, size_t *size) {
    Gen g = { .program = program };

    append(&g.text, "; Generated by vmc0\n    .include \"runtime.asm\"\n");
    for (int i = 0; i < program->assembly_count; i++) {
        append(&g.text, "    .include ");
        quoted(&g.text, program->assembly[i], strlen(program->assembly[i]));
        append(&g.text, "\n");
    }
    append(&g.text, "\n.text\n");
    mark_used(program);
    gen_entry(&g);
    for (int i = 0; i < program->module_count; i++) {
        g.m = program->modules[i];
        for (int k = 0; k < g.m->decl_count; k++) {
            Decl *d = g.m->decls[k];
            if (d->kind == DECL_FN && d->body && d->used) {
                gen_function(&g, d);
            }
        }
        for (int k = 0; k < g.m->decl_count; k++) {
            if (g.m->decls[k]->kind == DECL_VAR && g.m->decls[k]->used) {
                gen_global(&g, g.m->decls[k]);
            }
        }
    }
    append(&g.text, "\n.data\n%.*s%.*s", (int)g.data.size, g.data.bytes ? g.data.bytes : "", (int)g.pool.size,
           g.pool.bytes ? g.pool.bytes : "");
    free(g.data.bytes);
    free(g.pool.bytes);
    free(g.body.bytes);
    *size = g.text.size;
    return g.text.bytes;
}
