#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asm.h"

static int is_ident_start(int c) {
    return isalpha(c) || c == '_' || c == '.';
}

static int is_ident_char(int c) {
    return isalnum(c) || c == '_' || c == '.';
}

static int digit_value(int c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return 99;
}

// Decodes the escape sequence after a backslash; returns -1 if it is invalid
static int parse_escape(const char **p) {
    char c = *(*p)++;
    switch (c) {
        case 'n': return '\n';
        case 't': return '\t';
        case 'r': return '\r';
        case '0': return 0;
        case 'a': return 7;
        case 'b': return 8;
        case 'f': return 12;
        case 'v': return 11;
        case 'e': return 27;
        case '\\':
        case '"':
        case '\'':
            return c;
        case 'x': {
            int value = 0, digits = 0;
            while (digits < 2 && digit_value(**p) < 16) {
                value = value * 16 + digit_value(*(*p)++);
                digits++;
            }
            return digits ? value : -1;
        }
        default:
            return -1;
    }
}

static int parse_number(const char **p, int64_t *value) {
    const char *s = *p;
    int base = 10;

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    } else if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
        base = 2;
        s += 2;
    } else if (s[0] == '0' && (s[1] == 'o' || s[1] == 'O')) {
        base = 8;
        s += 2;
    } else if (s[0] == '0' && isdigit((unsigned char)s[1])) {
        base = 8;
        s += 1;
    }

    const char *digits = s;
    uint64_t result = 0;
    while (is_ident_char((unsigned char)*s)) {
        int d = digit_value(*s);
        if (d >= base || result > (UINT64_MAX - (uint64_t)d) / (uint64_t)base || result * base + d > INT64_MAX) {
            return 0;
        }
        result = result * base + d;
        s++;
    }
    if (s == digits) {
        return 0;
    }

    *value = (int64_t)result;
    *p = s;
    return 1;
}

int lex_line(const char *line, TokenList *out, char *error, size_t error_size) {
    size_t len = strlen(line);
    const char *p = line;

    out->items = malloc((len + 1) * sizeof(Token));
    out->arena = malloc(len * 2 + 2);
    out->count = 0;
    if (!out->items || !out->arena) {
        snprintf(error, error_size, "out of memory");
        tokens_free(out);
        return 0;
    }
    char *arena = out->arena;

    while (*p) {
        if (isspace((unsigned char)*p)) {
            p++;
            continue;
        }
        if (*p == ';') {
            break;
        }

        Token *t = &out->items[out->count++];
        memset(t, 0, sizeof(*t));

        if (is_ident_start((unsigned char)*p)) {
            t->kind = TOK_IDENT;
            t->text = arena;
            while (is_ident_char((unsigned char)*p)) {
                *arena++ = *p++;
            }
            *arena++ = '\0';
        } else if (isdigit((unsigned char)*p)) {
            t->kind = TOK_NUMBER;
            if (!parse_number(&p, &t->number)) {
                snprintf(error, error_size, "invalid number");
                tokens_free(out);
                return 0;
            }
        } else if (*p == '"') {
            t->kind = TOK_STRING;
            t->text = arena;
            p++;
            while (*p && *p != '"') {
                if (*p == '\\') {
                    p++;
                    int c = parse_escape(&p);
                    if (c < 0) {
                        snprintf(error, error_size, "invalid escape sequence in string");
                        tokens_free(out);
                        return 0;
                    }
                    *arena++ = (char)c;
                } else {
                    *arena++ = *p++;
                }
            }
            if (*p != '"') {
                snprintf(error, error_size, "unterminated string");
                tokens_free(out);
                return 0;
            }
            p++;
            t->length = (size_t)(arena - t->text);
            *arena++ = '\0';
        } else if (*p == '\'') {
            int c;
            p++;
            if (*p == '\\') {
                p++;
                c = parse_escape(&p);
            } else {
                c = *p ? (unsigned char)*p++ : -1;
            }
            if (c < 0 || *p != '\'') {
                snprintf(error, error_size, "invalid character literal");
                tokens_free(out);
                return 0;
            }
            p++;
            t->kind = TOK_NUMBER;
            t->number = c;
        } else if ((p[0] == '<' && p[1] == '<') || (p[0] == '>' && p[1] == '>')) {
            t->kind = TOK_PUNCT;
            t->punct = p[0] == '<' ? OP_SHL : OP_SHR;
            p += 2;
        } else if (strchr(",:[]()+-*/%&|^~#$=", *p)) {
            t->kind = TOK_PUNCT;
            t->punct = *p++;
        } else {
            snprintf(error, error_size, "unexpected character '%c'", *p);
            tokens_free(out);
            return 0;
        }
    }

    Token *end = &out->items[out->count];
    memset(end, 0, sizeof(*end));
    end->kind = TOK_END;
    return 1;
}

void tokens_free(TokenList *list) {
    free(list->items);
    free(list->arena);
    list->items = NULL;
    list->arena = NULL;
    list->count = 0;
}

int token_is_punct(const Token *token, int punct) {
    return token->kind == TOK_PUNCT && token->punct == punct;
}

int name_equals(const char *a, const char *b) {
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++;
        b++;
    }
    return tolower((unsigned char)*a) == tolower((unsigned char)*b);
}
