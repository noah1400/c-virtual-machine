#include <stdio.h>
#include <string.h>
#include "ore.h"

typedef struct {
    Program *program;
    Module *m;
    int pos;
    int no_literal;         // a name followed by { is not a struct literal, as in a case
} Parser;

static Stmt *parse_block(Parser *p);
static Expr *parse_expr(Parser *p);

static Token *peek(Parser *p) {
    return &p->m->tokens[p->pos];
}

static Token *peek_next(Parser *p) {
    return &p->m->tokens[p->pos + (p->pos + 1 < p->m->token_count)];
}

static int check(Parser *p, TokenKind kind) {
    return peek(p)->kind == kind;
}

static int accept(Parser *p, TokenKind kind) {
    if (check(p, kind)) {
        p->pos++;
        return 1;
    }
    return 0;
}

static const char *found(const Token *t) {
    static char text[48];
    if (t->kind == TOK_IDENT) {
        snprintf(text, sizeof(text), "'%.*s'", t->length > 32 ? 32 : t->length, t->text);
        return text;
    }
    return token_text(t->kind);
}

static _Noreturn void expected(Parser *p, const char *what) {
    fail(p->m, peek(p)->line, "expected %s, found %s", what, found(peek(p)));
}

static Token *expect(Parser *p, TokenKind kind, const char *where) {
    if (!check(p, kind)) {
        char what[64];
        snprintf(what, sizeof(what), "%s%s%s", token_text(kind), *where ? " " : "", where);
        expected(p, what);
    }
    return &p->m->tokens[p->pos++];
}

static const char *name(Parser *p, const char *what) {
    if (!check(p, TOK_IDENT)) {
        expected(p, what);
    }
    Token *t = &p->m->tokens[p->pos++];
    return copy_text(t->text, (size_t)t->length);
}

static int is_named(const Token *t, const char *text) {
    return t->kind == TOK_IDENT && (size_t)t->length == strlen(text) && strncmp(t->text, text, (size_t)t->length) == 0;
}

static TypeExpr *new_type(Parser *p, TypeExprKind kind) {
    TypeExpr *t = allocate(sizeof(TypeExpr));
    t->kind = kind;
    t->line = peek(p)->line;
    return t;
}

static int starts_type(Parser *p) {
    TokenKind k = peek(p)->kind;
    return k == TOK_IDENT || k == TOK_STAR || k == TOK_LBRACKET || k == TOK_FN;
}

static TypeExpr *parse_type(Parser *p) {
    TypeExpr *t = new_type(p, TE_NAME);
    if (accept(p, TOK_STAR)) {
        t->kind = TE_POINTER;
        t->base = parse_type(p);
    } else if (accept(p, TOK_LBRACKET)) {
        if (accept(p, TOK_RBRACKET)) {
            t->kind = TE_SLICE;
        } else {
            t->kind = TE_ARRAY;
            t->length = parse_expr(p);
            expect(p, TOK_RBRACKET, "after the length of the array");
        }
        t->base = parse_type(p);
    } else if (accept(p, TOK_FN)) {
        t->kind = TE_FN;
        expect(p, TOK_LPAREN, "after fn");
        int capacity = 0;
        while (!check(p, TOK_RPAREN)) {
            extend((void **)&t->params, &capacity, t->param_count, sizeof(TypeExpr *));
            t->params[t->param_count++] = parse_type(p);
            if (!accept(p, TOK_COMMA)) {
                break;
            }
        }
        expect(p, TOK_RPAREN, "after the parameter types");
        if (starts_type(p)) {
            t->base = parse_type(p);
        }
    } else {
        t->name = name(p, "a type");
        if (accept(p, TOK_DOT)) {
            t->qualifier = t->name;
            t->name = name(p, "a type");
        }
    }
    return t;
}

static Expr *new_expr(ExprKind kind, int line) {
    Expr *e = allocate(sizeof(Expr));
    e->kind = kind;
    e->line = line;
    return e;
}

static void add_arg(Expr *e, Expr *arg, const char *field, int *capacity) {
    int fields = *capacity;
    extend((void **)&e->fields, &fields, e->arg_count, sizeof(char *));
    extend((void **)&e->args, capacity, e->arg_count, sizeof(Expr *));
    e->fields[e->arg_count] = field;
    e->args[e->arg_count++] = arg;
}

// Inside brackets a name followed by { is a struct literal again
static Expr *parse_nested(Parser *p) {
    int saved = p->no_literal;
    p->no_literal = 0;
    Expr *e = parse_expr(p);
    p->no_literal = saved;
    return e;
}

static Expr *parse_elements(Parser *p, Expr *e) {
    int capacity = 0;
    expect(p, TOK_LBRACE, "before the elements");
    while (!check(p, TOK_RBRACE)) {
        add_arg(e, parse_nested(p), NULL, &capacity);
        if (!accept(p, TOK_COMMA)) {
            break;
        }
    }
    expect(p, TOK_RBRACE, "after the elements");
    return e;
}

static Expr *parse_struct_literal(Parser *p, Expr *type_name) {
    Expr *e = new_expr(EX_STRUCT, type_name->line);
    TypeExpr *t = allocate(sizeof(TypeExpr));
    t->kind = TE_NAME;
    t->line = type_name->line;
    if (type_name->kind == EX_FIELD) {
        t->qualifier = type_name->left->name;
    }
    t->name = type_name->name;
    e->type_expr = t;

    int capacity = 0;
    expect(p, TOK_LBRACE, "");
    while (!check(p, TOK_RBRACE)) {
        const char *field = name(p, "a field name");
        expect(p, TOK_COLON, "after the field name");
        add_arg(e, parse_nested(p), field, &capacity);
        if (!accept(p, TOK_COMMA)) {
            break;
        }
    }
    expect(p, TOK_RBRACE, "after the fields");
    return e;
}

static Expr *parse_primary(Parser *p) {
    Token *t = peek(p);
    Expr *e;

    switch (t->kind) {
        case TOK_INT:
            e = new_expr(EX_INT, t->line);
            e->value = t->value;
            p->pos++;
            return e;
        case TOK_STRING:
            e = new_expr(EX_STRING, t->line);
            e->bytes = t->bytes;
            e->size = t->size;
            p->pos++;
            return e;
        case TOK_TRUE:
        case TOK_FALSE:
            e = new_expr(EX_BOOL, t->line);
            e->value = t->kind == TOK_TRUE;
            p->pos++;
            return e;
        case TOK_NULL:
            p->pos++;
            return new_expr(EX_NULL, t->line);
        case TOK_LPAREN:
            p->pos++;
            e = parse_nested(p);
            expect(p, TOK_RPAREN, "");
            return e;
        case TOK_LBRACKET:
            e = new_expr(EX_ARRAY, t->line);
            e->type_expr = parse_type(p);
            return parse_elements(p, e);
        case TOK_IDENT:
            if ((is_named(t, "sizeof") || is_named(t, "new")) && peek_next(p)->kind == TOK_LPAREN) {
                e = new_expr(is_named(t, "new") ? EX_NEW : EX_SIZEOF, t->line);
                p->pos += 2;
                e->type_expr = parse_type(p);
                if (e->kind == EX_NEW && accept(p, TOK_COMMA)) {
                    e->left = parse_nested(p);
                }
                expect(p, TOK_RPAREN, "");
                return e;
            }
            e = new_expr(EX_NAME, t->line);
            e->name = name(p, "a name");
            return e;
        default:
            expected(p, "an expression");
    }
}

static Expr *parse_postfix(Parser *p) {
    Expr *e = parse_primary(p);
    for (;;) {
        int line = peek(p)->line;
        if (accept(p, TOK_LPAREN)) {
            Expr *call = new_expr(EX_CALL, line);
            int capacity = 0;
            call->left = e;
            while (!check(p, TOK_RPAREN)) {
                add_arg(call, parse_nested(p), NULL, &capacity);
                if (!accept(p, TOK_COMMA)) {
                    break;
                }
            }
            expect(p, TOK_RPAREN, "after the arguments");
            e = call;
        } else if (accept(p, TOK_LBRACKET)) {
            Expr *index = new_expr(EX_INDEX, line);
            index->left = e;
            if (!check(p, TOK_COLON)) {
                index->right = parse_nested(p);
            }
            if (accept(p, TOK_COLON)) {
                index->kind = EX_SLICE;
                if (!check(p, TOK_RBRACKET)) {
                    index->third = parse_nested(p);
                }
            }
            expect(p, TOK_RBRACKET, "");
            e = index;
        } else if (accept(p, TOK_DOT)) {
            Expr *field = new_expr(EX_FIELD, line);
            field->left = e;
            field->name = name(p, "a field name");
            e = field;
        } else if (check(p, TOK_LBRACE) && !p->no_literal &&
                   (e->kind == EX_NAME || (e->kind == EX_FIELD && e->left->kind == EX_NAME))) {
            e = parse_struct_literal(p, e);
        } else {
            return e;
        }
    }
}

static Expr *parse_unary(Parser *p) {
    Token *t = peek(p);
    if (t->kind == TOK_MINUS || t->kind == TOK_BANG || t->kind == TOK_TILDE || t->kind == TOK_STAR ||
        t->kind == TOK_AMP) {
        Expr *e = new_expr(EX_UNARY, t->line);
        e->op = t->kind;
        p->pos++;
        e->left = parse_unary(p);
        return e;
    }
    return parse_postfix(p);
}

static Expr *parse_cast(Parser *p) {
    Expr *e = parse_unary(p);
    while (check(p, TOK_AS)) {
        Expr *cast = new_expr(EX_CAST, peek(p)->line);
        p->pos++;
        cast->left = e;
        cast->type_expr = parse_type(p);
        e = cast;
    }
    return e;
}

static int precedence(TokenKind kind) {
    switch (kind) {
        case TOK_OROR:
            return 1;
        case TOK_ANDAND:
            return 2;
        case TOK_EQ:
        case TOK_NE:
        case TOK_LT:
        case TOK_LE:
        case TOK_GT:
        case TOK_GE:
            return 3;
        case TOK_PIPE:
            return 4;
        case TOK_CARET:
            return 5;
        case TOK_AMP:
            return 6;
        case TOK_SHL:
        case TOK_SHR:
            return 7;
        case TOK_PLUS:
        case TOK_MINUS:
            return 8;
        case TOK_STAR:
        case TOK_SLASH:
        case TOK_PERCENT:
            return 9;
        default:
            return 0;
    }
}

static Expr *parse_binary(Parser *p, int lowest) {
    Expr *left = parse_cast(p);
    for (;;) {
        Token *t = peek(p);
        int level = precedence(t->kind);
        if (level == 0 || level < lowest) {
            return left;
        }
        Expr *e = new_expr(EX_BINARY, t->line);
        e->op = t->kind;
        p->pos++;
        e->left = left;
        e->right = parse_binary(p, level + 1);
        left = e;
        if (level == 3 && precedence(peek(p)->kind) == 3) {
            fail(p->m, peek(p)->line, "comparisons cannot be chained; join them with && or ||");
        }
    }
}

static Expr *parse_expr(Parser *p) {
    return parse_binary(p, 1);
}

static Stmt *new_stmt(StmtKind kind, int line) {
    Stmt *s = allocate(sizeof(Stmt));
    s->kind = kind;
    s->line = line;
    return s;
}

static int assignment(TokenKind kind) {
    return kind >= TOK_ASSIGN && kind <= TOK_SHR_ASSIGN;
}

// An assignment or a call
static Stmt *parse_simple(Parser *p) {
    int line = peek(p)->line;
    Expr *e = parse_expr(p);
    if (assignment(peek(p)->kind)) {
        Stmt *s = new_stmt(ST_ASSIGN, line);
        s->op = peek(p)->kind;
        p->pos++;
        s->target = e;
        s->value = parse_expr(p);
        return s;
    }
    if (e->kind != EX_CALL) {
        fail(p->m, line, "only calls and assignments can be statements");
    }
    Stmt *s = new_stmt(ST_EXPR, line);
    s->value = e;
    return s;
}

// var and const without the semicolon, which a for loop leaves out
static Stmt *parse_variable(Parser *p) {
    Token *t = peek(p);
    Stmt *s = new_stmt(t->kind == TOK_CONST ? ST_CONST : ST_VAR, t->line);
    p->pos++;
    s->name = name(p, "a name");
    if (accept(p, TOK_COLON)) {
        s->type_expr = parse_type(p);
    }
    if (accept(p, TOK_ASSIGN)) {
        s->value = parse_expr(p);
    } else if (s->kind == ST_CONST) {
        expected(p, "'=' and the value of the constant");
    } else if (!s->type_expr) {
        expected(p, "a type or '='");
    }
    return s;
}

static Stmt *parse_if(Parser *p) {
    Stmt *s = new_stmt(ST_IF, peek(p)->line);
    p->pos++;
    expect(p, TOK_LPAREN, "after if");
    s->cond = parse_expr(p);
    expect(p, TOK_RPAREN, "after the condition");
    s->then = parse_block(p);
    if (accept(p, TOK_ELSE)) {
        s->otherwise = check(p, TOK_IF) ? parse_if(p) : parse_block(p);
    }
    return s;
}

static Stmt *parse_switch(Parser *p) {
    Stmt *s = new_stmt(ST_SWITCH, peek(p)->line);
    int capacity = 0;

    p->pos++;
    expect(p, TOK_LPAREN, "after switch");
    s->cond = parse_expr(p);
    expect(p, TOK_RPAREN, "after the value");
    expect(p, TOK_LBRACE, "before the cases");
    while (!accept(p, TOK_RBRACE)) {
        if (check(p, TOK_DEFAULT)) {
            if (s->fallback) {
                fail(p->m, peek(p)->line, "the switch already has a default");
            }
            p->pos++;
            s->fallback = parse_block(p);
            continue;
        }
        int line = expect(p, TOK_CASE, "or default")->line;
        extend((void **)&s->cases, &capacity, s->case_count, sizeof(Case));
        Case *c = &s->cases[s->case_count++];
        int values = 0;
        c->line = line;
        p->no_literal = 1;
        do {
            extend((void **)&c->values, &values, c->value_count, sizeof(Expr *));
            c->values[c->value_count++] = parse_expr(p);
        } while (accept(p, TOK_COMMA));
        p->no_literal = 0;
        c->body = parse_block(p);
    }
    return s;
}

static Stmt *parse_stmt(Parser *p) {
    Token *t = peek(p);
    Stmt *s;

    switch (t->kind) {
        case TOK_LBRACE:
            return parse_block(p);
        case TOK_VAR:
        case TOK_CONST:
            s = parse_variable(p);
            break;
        case TOK_IF:
            return parse_if(p);
        case TOK_WHILE:
            s = new_stmt(ST_WHILE, t->line);
            p->pos++;
            expect(p, TOK_LPAREN, "after while");
            s->cond = parse_expr(p);
            expect(p, TOK_RPAREN, "after the condition");
            s->then = parse_block(p);
            return s;
        case TOK_FOR:
            s = new_stmt(ST_FOR, t->line);
            p->pos++;
            expect(p, TOK_LPAREN, "after for");
            if (!check(p, TOK_SEMI)) {
                s->init = check(p, TOK_VAR) ? parse_variable(p) : parse_simple(p);
            }
            expect(p, TOK_SEMI, "after the start of the loop");
            if (!check(p, TOK_SEMI)) {
                s->cond = parse_expr(p);
            }
            expect(p, TOK_SEMI, "after the condition");
            if (!check(p, TOK_RPAREN)) {
                s->step = parse_simple(p);
            }
            expect(p, TOK_RPAREN, "after the step");
            s->then = parse_block(p);
            return s;
        case TOK_SWITCH:
            return parse_switch(p);
        case TOK_BREAK:
        case TOK_CONTINUE:
            s = new_stmt(t->kind == TOK_BREAK ? ST_BREAK : ST_CONTINUE, t->line);
            p->pos++;
            break;
        case TOK_RETURN:
            s = new_stmt(ST_RETURN, t->line);
            p->pos++;
            if (!check(p, TOK_SEMI)) {
                s->value = parse_expr(p);
            }
            break;
        default:
            s = parse_simple(p);
            break;
    }
    expect(p, TOK_SEMI, "after the statement");
    return s;
}

static Stmt *parse_block(Parser *p) {
    Stmt *s = new_stmt(ST_BLOCK, expect(p, TOK_LBRACE, "")->line);
    int capacity = 0;
    while (!check(p, TOK_RBRACE)) {
        if (check(p, TOK_EOF)) {
            fail(p->m, s->line, "the block is missing its '}'");
        }
        extend((void **)&s->body, &capacity, s->count, sizeof(Stmt *));
        s->body[s->count++] = parse_stmt(p);
    }
    s->end_line = peek(p)->line;
    p->pos++;
    return s;
}

static void parse_params(Parser *p, Decl *d) {
    int capacity = 0;
    expect(p, TOK_LPAREN, "after the name of the function");
    while (!check(p, TOK_RPAREN)) {
        extend((void **)&d->params, &capacity, d->param_count, sizeof(Param));
        Param *param = &d->params[d->param_count++];
        param->line = peek(p)->line;
        param->name = name(p, "a parameter name");
        expect(p, TOK_COLON, "after the parameter name");
        param->type_expr = parse_type(p);
        if (!accept(p, TOK_COMMA)) {
            break;
        }
    }
    expect(p, TOK_RPAREN, "after the parameters");
    if (starts_type(p)) {
        d->type_expr = parse_type(p);
    }
}

static void parse_struct(Parser *p, Decl *d) {
    int capacity = 0;
    d->name = name(p, "the name of the struct");
    expect(p, TOK_LBRACE, "after the name of the struct");
    while (!accept(p, TOK_RBRACE)) {
        extend((void **)&d->params, &capacity, d->param_count, sizeof(Param));
        Param *field = &d->params[d->param_count++];
        field->line = peek(p)->line;
        field->name = name(p, "a field name");
        expect(p, TOK_COLON, "after the field name");
        field->type_expr = parse_type(p);
        expect(p, TOK_SEMI, "after the field");
    }
}

static void parse_enum(Parser *p, Decl *d) {
    int capacity = 0;
    d->name = name(p, "the name of the enum");
    expect(p, TOK_LBRACE, "after the name of the enum");
    while (!check(p, TOK_RBRACE)) {
        extend((void **)&d->members, &capacity, d->member_count, sizeof(Member));
        Member *member = &d->members[d->member_count++];
        member->line = peek(p)->line;
        member->name = name(p, "a name");
        if (accept(p, TOK_ASSIGN)) {
            member->value = parse_expr(p);
        }
        if (!accept(p, TOK_COMMA)) {
            break;
        }
    }
    expect(p, TOK_RBRACE, "after the values of the enum");
}

static Decl *parse_decl(Parser *p) {
    Decl *d = allocate(sizeof(Decl));
    d->module = p->m;
    d->is_pub = accept(p, TOK_PUB);
    d->line = peek(p)->line;

    Token *t = peek(p);
    switch (t->kind) {
        case TOK_IMPORT:
            if (d->is_pub) {
                fail(p->m, d->line, "imports cannot be pub");
            }
            p->pos++;
            d->kind = DECL_IMPORT;
            d->path = expect(p, TOK_STRING, "after import")->bytes;
            if (accept(p, TOK_AS)) {
                d->name = name(p, "a name for the module");
            }
            expect(p, TOK_SEMI, "after the import");
            break;
        case TOK_CONST:
        case TOK_VAR: {
            Stmt *s = parse_variable(p);
            expect(p, TOK_SEMI, "after the declaration");
            d->kind = s->kind == ST_CONST ? DECL_CONST : DECL_VAR;
            d->name = s->name;
            d->type_expr = s->type_expr;
            d->value = s->value;
            break;
        }
        case TOK_EXTERN:
        case TOK_INTERRUPT:
            d->is_extern = t->kind == TOK_EXTERN;
            d->is_interrupt = t->kind == TOK_INTERRUPT;
            p->pos++;
            if (!check(p, TOK_FN)) {
                expected(p, "fn");
            }
            // fall through
        case TOK_FN:
            p->pos++;
            d->kind = DECL_FN;
            d->name = name(p, "the name of the function");
            parse_params(p, d);
            if (d->is_extern) {
                expect(p, TOK_SEMI, "after an extern function");
            } else {
                d->body = parse_block(p);
            }
            break;
        case TOK_STRUCT:
            p->pos++;
            d->kind = DECL_STRUCT;
            parse_struct(p, d);
            break;
        case TOK_ENUM:
            p->pos++;
            d->kind = DECL_ENUM;
            parse_enum(p, d);
            break;
        default:
            expected(p, "a declaration");
    }
    return d;
}

void parse(Program *program, Module *m) {
    Parser p = { program, m, 0, 0 };
    int capacity = 0;

    while (!check(&p, TOK_EOF)) {
        extend((void **)&m->decls, &capacity, m->decl_count, sizeof(Decl *));
        m->decls[m->decl_count++] = parse_decl(&p);
    }
    for (int i = 0; i < m->decl_count; i++) {
        Decl *d = m->decls[i];
        if (d->kind == DECL_IMPORT) {
            d->imported = load_module(program, d->path, m, d->line);
            if (!d->name) {
                d->name = d->imported->name;
            }
        }
    }
}
