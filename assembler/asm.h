#ifndef _ASM_H_
#define _ASM_H_

#include <stddef.h>
#include <stdint.h>
#include "instruction_set.h"

#define ASM_MAX_INCLUDE_DEPTH 16
#define ASM_MAX_INCLUDE_DIRS  16
#define ASM_MAX_ERRORS        50

typedef enum { TOK_END, TOK_IDENT, TOK_NUMBER, TOK_STRING, TOK_PUNCT } TokenKind;

// Two-character operators; single-character punctuation uses the character itself
enum { OP_SHL = 256, OP_SHR };

typedef struct {
    TokenKind kind;
    int punct;
    int64_t number;
    char *text;         // identifier or string bytes, NUL-terminated
    size_t length;      // string length, which may include embedded NULs
    size_t start;       // offset of the token in the line
} Token;

typedef struct {
    Token *items;
    int count;
    size_t end;         // offset where the statement ends, before any comment
    char *arena;        // storage for token text
} TokenList;

typedef enum {
    LINE_SOURCE,
    LINE_INCLUDE_BEGIN,
    LINE_INCLUDE_END,
    LINE_MACRO_DEFINITION,  // shown in listings, otherwise ignored
    LINE_MACRO_CALL,        // only its labels are assembled; the expansion follows
} LineKind;

typedef struct {
    LineKind kind;
    const char *file;
    int number;
    char *text;
    int expanded;           // produced by a macro expansion
} SourceLine;

typedef struct {
    char *name;
    char **params;
    int param_count;
    char **body;
    int body_count;
} Macro;

typedef enum { SYM_CODE = 0, SYM_DATA = 1, SYM_CONST = 2 } SymbolKind;

typedef struct {
    char *name;
    int64_t value;
    int defined;
    SymbolKind kind;
    const SourceLine *line;
} AsmSymbol;

typedef struct {
    AsmSymbol *items;
    size_t count;
    size_t capacity;
    size_t *buckets;    // symbol index + 1, 0 when empty
    size_t bucket_count;
} SymbolTable;

enum { SECTION_TEXT, SECTION_DATA, SECTION_COUNT };

typedef struct {
    const char *name;
    uint32_t base;
    uint32_t limit;     // one past the last usable address
    uint32_t pc;
    uint32_t end;       // one past the highest address written
    uint8_t *bytes;
} Section;

// What each source line produced, recorded in pass 1 and filled in by pass 2
typedef struct {
    int section;        // -1 when the line emitted nothing
    uint32_t address;
    uint32_t size;
    int is_code;
    int wide;           // pseudo-instruction expanded to two words
} LineResult;

typedef struct {
    const char *include_dirs[ASM_MAX_INCLUDE_DIRS];
    int include_dir_count;

    SourceLine *lines;
    size_t line_count;
    size_t line_capacity;
    char **files;
    size_t file_count;
    Macro *macros;
    size_t macro_count;
    unsigned expansions;

    int pass;
    Section sections[SECTION_COUNT];
    int section;
    int section_stack[ASM_MAX_INCLUDE_DEPTH + 1];
    int section_depth;
    char scope[128];
    SymbolTable symbols;
    LineResult *results;

    struct PendingConstant *pending;
    size_t pending_count;
    int64_t entry;
    const SourceLine *entry_line;   // the .entry directive, if any

    const SourceLine *line;
    uint32_t statement_address;
    int errors;
} Assembler;

// lexer.c
int lex_line(const char *line, TokenList *out, char *error, size_t error_size);
void tokens_free(TokenList *list);
int token_is_punct(const Token *token, int punct);
int name_equals(const char *a, const char *b);

// symbols.c
void symbols_init(SymbolTable *table);
void symbols_free(SymbolTable *table);
AsmSymbol *symbols_find(SymbolTable *table, const char *name);
AsmSymbol *symbols_add(SymbolTable *table, const char *name);

// expr.c
typedef struct {
    Assembler *as;
    TokenList *tokens;
    int pos;
    int unresolved;     // an undefined symbol was referenced during pass 1
    int failed;
} Parser;

int64_t parse_expression(Parser *p);
int qualify_name(Assembler *as, const char *name, char *out, size_t size);

// source.c
int source_load(Assembler *as, const char *path);

// assemble.c
void asm_init(Assembler *as);
void asm_free(Assembler *as);
int asm_assemble(Assembler *as, const char *path);
void asm_error(Assembler *as, const char *format, ...);

// output.c
int output_binary(Assembler *as, const char *path, int with_debug);
int output_listing(Assembler *as, const char *path);
void output_symbols(Assembler *as);

#endif // _ASM_H_
