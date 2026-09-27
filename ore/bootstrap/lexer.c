#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "ore.h"

static const struct {
    const char *text;
    TokenKind kind;
} keywords[] = {
    { "as", TOK_AS },           { "break", TOK_BREAK },   { "case", TOK_CASE },     { "const", TOK_CONST },
    { "continue", TOK_CONTINUE }, { "default", TOK_DEFAULT }, { "else", TOK_ELSE }, { "enum", TOK_ENUM },
    { "extern", TOK_EXTERN },   { "false", TOK_FALSE },   { "fn", TOK_FN },         { "for", TOK_FOR },
    { "if", TOK_IF },           { "import", TOK_IMPORT }, { "interrupt", TOK_INTERRUPT }, { "null", TOK_NULL },
    { "pub", TOK_PUB },         { "return", TOK_RETURN }, { "struct", TOK_STRUCT }, { "switch", TOK_SWITCH },
    { "true", TOK_TRUE },       { "var", TOK_VAR },       { "while", TOK_WHILE },
};

static const char *const punctuation[] = {
    [TOK_LPAREN] = "(",   [TOK_RPAREN] = ")",   [TOK_LBRACE] = "{",    [TOK_RBRACE] = "}",
    [TOK_LBRACKET] = "[", [TOK_RBRACKET] = "]", [TOK_COMMA] = ",",     [TOK_SEMI] = ";",
    [TOK_COLON] = ":",    [TOK_DOT] = ".",      [TOK_PLUS] = "+",      [TOK_MINUS] = "-",
    [TOK_STAR] = "*",     [TOK_SLASH] = "/",    [TOK_PERCENT] = "%",   [TOK_AMP] = "&",
    [TOK_PIPE] = "|",     [TOK_CARET] = "^",    [TOK_TILDE] = "~",     [TOK_BANG] = "!",
    [TOK_SHL] = "<<",     [TOK_SHR] = ">>",     [TOK_ANDAND] = "&&",   [TOK_OROR] = "||",
    [TOK_EQ] = "==",      [TOK_NE] = "!=",      [TOK_LT] = "<",        [TOK_LE] = "<=",
    [TOK_GT] = ">",       [TOK_GE] = ">=",      [TOK_ASSIGN] = "=",    [TOK_ADD_ASSIGN] = "+=",
    [TOK_SUB_ASSIGN] = "-=", [TOK_MUL_ASSIGN] = "*=", [TOK_DIV_ASSIGN] = "/=", [TOK_MOD_ASSIGN] = "%=",
    [TOK_AND_ASSIGN] = "&=", [TOK_OR_ASSIGN] = "|=",  [TOK_XOR_ASSIGN] = "^=", [TOK_SHL_ASSIGN] = "<<=",
    [TOK_SHR_ASSIGN] = ">>=",
};

const char *token_text(TokenKind kind) {
    static char quoted[TOK_SHR_ASSIGN + 1][8];
    switch (kind) {
        case TOK_EOF:
            return "the end of the file";
        case TOK_IDENT:
            return "a name";
        case TOK_INT:
            return "a number";
        case TOK_STRING:
            return "a string";
        default:
            break;
    }
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        if (keywords[i].kind == kind) {
            return keywords[i].text;
        }
    }
    snprintf(quoted[kind], sizeof(quoted[kind]), "'%s'", punctuation[kind]);
    return quoted[kind];
}

typedef struct {
    Module *m;
    const char *p;
    int line;
    int capacity;
} Lexer;

static void add(Lexer *lx, Token token) {
    Module *m = lx->m;
    extend((void **)&m->tokens, &lx->capacity, m->token_count, sizeof(Token));
    m->tokens[m->token_count++] = token;
}

static int digit_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99;
}

static int escape(Lexer *lx) {
    char c = *lx->p++;
    switch (c) {
        case 'n':
            return '\n';
        case 't':
            return '\t';
        case 'r':
            return '\r';
        case '0':
            return 0;
        case '\\':
        case '\'':
        case '"':
            return c;
        case '\0':
            fail(lx->m, lx->line, "unterminated escape sequence");
        case 'x':
            if (digit_value(lx->p[0]) < 16 && digit_value(lx->p[1]) < 16) {
                lx->p += 2;
                return digit_value(lx->p[-2]) * 16 + digit_value(lx->p[-1]);
            }
            fail(lx->m, lx->line, "\\x needs two hexadecimal digits");
        default:
            fail(lx->m, lx->line, "unknown escape sequence \\%c", c);
    }
}

static void number(Lexer *lx, Token *t) {
    const char *start = lx->p;
    int base = 10, digits = 0;
    uint64_t value = 0;

    if (start[0] == '0' && (start[1] == 'x' || start[1] == 'X')) {
        base = 16;
        lx->p += 2;
    } else if (start[0] == '0' && (start[1] == 'b' || start[1] == 'B')) {
        base = 2;
        lx->p += 2;
    }
    for (;; lx->p++) {
        int d = digit_value(*lx->p);
        if (*lx->p == '_' && digits > 0) {
            continue;
        }
        if (d >= base) {
            break;
        }
        if (value > (UINT64_MAX - (uint64_t)d) / (uint64_t)base) {
            fail(lx->m, lx->line, "number too large");
        }
        value = value * (uint64_t)base + (uint64_t)d;
        digits++;
    }
    if ((*lx->p == '.' && isdigit((unsigned char)lx->p[1])) || (base == 10 && (*lx->p == 'e' || *lx->p == 'E'))) {
        fail(lx->m, lx->line, "floating-point numbers are not supported yet");
    }
    if (digits == 0 || lx->p[-1] == '_' || isalnum((unsigned char)*lx->p) || *lx->p == '_') {
        const char *end = lx->p;
        while (isalnum((unsigned char)*end) || *end == '_') {
            end++;
        }
        fail(lx->m, lx->line, "invalid number %.*s", (int)(end - start), start);
    }
    if (base == 10 && start[0] == '0' && digits > 1) {
        fail(lx->m, lx->line, "decimal numbers other than 0 do not start with 0");
    }
    if (value > INT64_MAX) {
        fail(lx->m, lx->line, "number too large");
    }
    t->kind = TOK_INT;
    t->value = (int64_t)value;
}

static void character(Lexer *lx, Token *t) {
    lx->p++;
    if (*lx->p == '\'' || *lx->p == '\n' || *lx->p == '\0') {
        fail(lx->m, lx->line, "a character literal holds one character");
    }
    int c = *lx->p == '\\' ? (lx->p++, escape(lx)) : (unsigned char)*lx->p++;
    if (*lx->p != '\'') {
        fail(lx->m, lx->line, "a character literal holds one character");
    }
    lx->p++;
    t->kind = TOK_INT;
    t->value = c;
}

static void string(Lexer *lx, Token *t) {
    int capacity = 0;
    lx->p++;
    while (*lx->p != '"') {
        if (*lx->p == '\n' || *lx->p == '\0') {
            fail(lx->m, lx->line, "unterminated string");
        }
        int c = *lx->p == '\\' ? (lx->p++, escape(lx)) : (unsigned char)*lx->p++;
        extend((void **)&t->bytes, &capacity, t->size + 1, 1);
        t->bytes[t->size++] = (char)c;
    }
    extend((void **)&t->bytes, &capacity, t->size, 1);
    lx->p++;
    t->bytes[t->size] = '\0';
    t->kind = TOK_STRING;
}

// Longest punctuation first, so that <<= wins over << and <
static int punct(Lexer *lx, Token *t) {
    static const TokenKind order[] = {
        TOK_SHL_ASSIGN, TOK_SHR_ASSIGN, TOK_SHL,        TOK_SHR,        TOK_ANDAND,     TOK_OROR,
        TOK_EQ,         TOK_NE,         TOK_LE,         TOK_GE,         TOK_ADD_ASSIGN, TOK_SUB_ASSIGN,
        TOK_MUL_ASSIGN, TOK_DIV_ASSIGN, TOK_MOD_ASSIGN, TOK_AND_ASSIGN, TOK_OR_ASSIGN,  TOK_XOR_ASSIGN,
        TOK_LPAREN,     TOK_RPAREN,     TOK_LBRACE,     TOK_RBRACE,     TOK_LBRACKET,   TOK_RBRACKET,
        TOK_COMMA,      TOK_SEMI,       TOK_COLON,      TOK_DOT,        TOK_PLUS,       TOK_MINUS,
        TOK_STAR,       TOK_SLASH,      TOK_PERCENT,    TOK_AMP,        TOK_PIPE,       TOK_CARET,
        TOK_TILDE,      TOK_BANG,       TOK_LT,         TOK_GT,         TOK_ASSIGN,
    };
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
        size_t length = strlen(punctuation[order[i]]);
        if (strncmp(lx->p, punctuation[order[i]], length) == 0) {
            lx->p += length;
            t->kind = order[i];
            return 1;
        }
    }
    return 0;
}

static void skip_space(Lexer *lx) {
    for (;;) {
        if (*lx->p == '\n') {
            lx->line++;
            lx->p++;
        } else if (isspace((unsigned char)*lx->p)) {
            lx->p++;
        } else if (lx->p[0] == '/' && lx->p[1] == '/') {
            while (*lx->p && *lx->p != '\n') {
                lx->p++;
            }
        } else if (lx->p[0] == '/' && lx->p[1] == '*') {
            int start = lx->line;
            for (lx->p += 2; !(lx->p[0] == '*' && lx->p[1] == '/'); lx->p++) {
                if (*lx->p == '\0') {
                    fail(lx->m, start, "unterminated comment");
                }
                lx->line += *lx->p == '\n';
            }
            lx->p += 2;
        } else {
            return;
        }
    }
}

static void split_lines(Module *m) {
    int capacity = 0;
    for (const char *p = m->source;; p++) {
        if (p == m->source || p[-1] == '\n') {
            if (*p == '\0') {
                break;
            }
            extend((void **)&m->lines, &capacity, m->line_count, sizeof(char *));
            m->lines[m->line_count++] = p;
        }
        if (*p == '\0') {
            break;
        }
    }
}

void lex(Module *m) {
    Lexer lx = { m, m->source, 1, 0 };

    split_lines(m);
    for (;;) {
        skip_space(&lx);
        Token t = { .line = lx.line, .text = lx.p };
        char c = *lx.p;
        if (c == '\0') {
            t.kind = TOK_EOF;
            add(&lx, t);
            return;
        }
        if (isalpha((unsigned char)c) || c == '_') {
            while (isalnum((unsigned char)*lx.p) || *lx.p == '_') {
                lx.p++;
            }
            t.kind = TOK_IDENT;
            for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
                if ((size_t)(lx.p - t.text) == strlen(keywords[i].text) &&
                    strncmp(t.text, keywords[i].text, (size_t)(lx.p - t.text)) == 0) {
                    t.kind = keywords[i].kind;
                }
            }
        } else if (isdigit((unsigned char)c)) {
            number(&lx, &t);
        } else if (c == '\'') {
            character(&lx, &t);
        } else if (c == '"') {
            string(&lx, &t);
        } else if (!punct(&lx, &t)) {
            if (isprint((unsigned char)c)) {
                fail(m, lx.line, "unexpected character '%c'", c);
            }
            fail(m, lx.line, "unexpected byte 0x%02X", (unsigned char)c);
        }
        t.length = (int)(lx.p - t.text);
        add(&lx, t);
    }
}
