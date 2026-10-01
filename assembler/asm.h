#ifndef _ASM_H_
#define _ASM_H_

#include <stddef.h>
#include <stdint.h>
#include "instruction_set.h"

#define ASM_MAX_INCLUDE_DEPTH 16
#define ASM_MAX_INCLUDE_DIRS  16
#define ASM_MAX_DEFINES       32
#define ASM_MAX_CONDITIONS    32
#define ASM_MAX_INLINE_DEPTH  16
#define ASM_MAX_ERRORS        50
#define ASM_MAX_SECTION_SIZE  (16u * 1024 * 1024)

typedef enum { TOK_END, TOK_IDENT, TOK_NUMBER, TOK_STRING, TOK_PUNCT } TokenKind;

// Two-character operators; single-character punctuation uses the character itself
enum { OP_SHL = 256, OP_SHR, OP_EQ, OP_NE, OP_LE, OP_GE, OP_AND, OP_OR };

typedef struct {
    TokenKind kind;
    int punct;
    int64_t number;
    char *text;         // identifier or string bytes, NUL-terminated
    size_t length;      // string length, which may include embedded NULs
    size_t start;       // offset of the token in the line
    int is_float;       // number holds the bits of a single-precision float
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
    char **defaults;    // text used for a missing or empty argument, NULL where the argument is required
    int param_count;
    char **body;
    int body_count;
} Macro;

typedef enum { SYM_CODE = 0, SYM_DATA = 1, SYM_CONST = 2 } SymbolKind;

// What a value in an object file is relative to: nothing, a section, or BASE_SYMBOL plus the index of
// an external symbol. Binaries only hold plain values.
enum { BASE_NONE, BASE_TEXT, BASE_DATA, BASE_SYMBOL };

typedef struct {
    char *name;
    int64_t value;
    int defined;
    SymbolKind kind;
    int base;
    int external;       // declared with .extern and defined by another object file
    int global;         // exported with .global
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

// Pass 1 defines symbols, layout passes repeat until no label moves, the last pass emits bytes
enum { PASS_DEFINE = 1, PASS_LAYOUT, PASS_EMIT };

typedef struct {
    const char *name;
    uint32_t base;
    uint32_t pc;
    uint32_t end;       // one past the highest address written
    uint8_t *bytes;     // contents, allocated for the final pass
    uint32_t allocated;
    uint32_t alignment; // the largest .align used in it
} Section;

// What each source line produced; the final pass fills in the addresses
typedef struct {
    int section;        // -1 when the line emitted nothing
    uint32_t address;
    uint32_t size;
    int is_code;
    int extended;       // the instruction carries an extension word
    int condition;      // result of a conditional directive, decided in pass 1
    int64_t layout_value;   // size, alignment or address argument of a directive, fixed in pass 1
    int relocated;      // the linker supplies the instruction's immediate
    size_t location;    // 1 + index of the .loc in effect, 0 before any
    int unit;           // the unit of a library the line is in, 0 for none
    int removable;      // left out with its unit, unlike directives that every program keeps
    int dropped;        // its unit is left out
} LineResult;

// A line that refers to a symbol, or code that runs into the unit after it. from is the unit that
// does it, 0 for code that is always kept, and to the unit it needs.
typedef struct {
    int from;
    int to;
    char *name;         // the symbol, until it is looked up
} Use;

// In a file with .library, the code or data from one label to the next is a unit, which is left out
// unless something the program keeps refers to its labels or runs into it
typedef struct {
    const char **files;         // the files that declared .library
    size_t file_count;
    const char **units;         // the file of each unit; units count from 1
    size_t unit_count;
    Use *uses;
    size_t use_count;
    size_t use_capacity;
    int current[SECTION_COUNT]; // the unit that lines in each section belong to, 0 for none
    int last_code;              // unit of the last instruction or data in .text, -1 before any
    int runs_on;                // execution can continue past it
    int first_code;             // unit of the first code, where a program without .entry starts
} Library;

// A line of another source, such as the C file a compiler read, named by .loc
typedef struct {
    char *file;
    int line;
    char *text;
} Location;

// Code that a compiler copied from a function into a call of it at a line, from .inline to .endinline
typedef struct {
    int section;
    uint32_t start, end;
    char *file;
    int line;
} Inlined;

// A file that .loc names without the text of the line, read for its lines
typedef struct {
    char *path;
    char *text;         // with a NUL where each line ends, NULL when the file cannot be read
    char **lines;
    int line_count;
} LocatedFile;

// A value in an object file that the linker fills in
typedef struct {
    int section;
    int type;           // VMO_ABS32 or VMO_REL32
    uint32_t offset;
    int base;           // what the value is relative to
    int64_t addend;
} Relocation;

typedef struct {
    char *name;
    const SourceLine *line;
} GlobalName;

// State of one open .if block
typedef struct {
    int active;         // lines are assembled
    int taken;          // a branch of this block was already assembled
    int parent_active;
} Condition;

typedef struct {
    const char *include_dirs[ASM_MAX_INCLUDE_DIRS];
    int include_dir_count;
    const char *defines[ASM_MAX_DEFINES];   // NAME or NAME=VALUE from the command line
    int define_count;

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
    Condition conditions[ASM_MAX_CONDITIONS];
    int condition_depth;

    char structure[128];        // the .struct being defined, empty outside one
    int64_t structure_offset;
    const SourceLine *structure_line;

    int object;         // assembling an object file, where addresses are relative to their section
    uint32_t memory_kb; // the memory the program asks for, 0 for none
    Relocation *relocations;
    size_t relocation_count;
    size_t relocation_capacity;
    GlobalName *globals;
    size_t global_count;
    Location *locations;
    size_t location_count;
    size_t location_capacity;
    LocatedFile *located_files;
    size_t located_file_count;
    size_t location;    // 1 + index of the .loc in effect, 0 before any
    Inlined *inlined;   // the copies of calls that the final pass found
    size_t inlined_count;
    size_t inlined_capacity;
    size_t inline_open[ASM_MAX_INLINE_DEPTH];       // the copies not ended yet, innermost last
    const SourceLine *inline_lines[ASM_MAX_INLINE_DEPTH];
    int inline_depth;
    int changed;        // a layout pass moved a label, changed a constant or widened an instruction
    int64_t entry;
    const SourceLine *entry_line;   // the .entry directive, if any

    Library library;

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
int symbols_remove(SymbolTable *table, const unsigned char *removed);

// expr.c
typedef struct {
    Assembler *as;
    TokenList *tokens;
    int pos;
    int unresolved;     // an undefined symbol was referenced before the final pass
    int failed;
    int floats;         // float literals read so far
    int operators;      // operators applied so far, which floats must not take part in
    SymbolTable *constants;     // while the source is read: the constants known so far
    int quiet;          // leave errors to the passes
    int relocatable;    // the caller takes a value relative to a section or an external symbol
    int base;           // what the last expression is relative to
} Parser;

int64_t parse_expression(Parser *p);
int qualify_name(Assembler *as, const char *name, char *out, size_t size);

// source.c
char *read_text(const char *path);
int source_load(Assembler *as, const char *path);
char *source_find(Assembler *as, const char *includer, const char *name);

// assemble.c
void asm_init(Assembler *as);
void asm_free(Assembler *as);
int asm_assemble(Assembler *as, const char *path);
void asm_error(Assembler *as, const char *format, ...);
void asm_note_use(Assembler *as, const char *name);

// check.c
int check_registers(Assembler *as);

// output.c
int output_binary(Assembler *as, const char *path, int with_debug);
int output_object(Assembler *as, const char *path);
int output_listing(Assembler *as, const char *path);
void output_symbols(Assembler *as);

#endif // _ASM_H_
