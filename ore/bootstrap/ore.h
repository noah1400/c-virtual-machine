#ifndef _ORE_H_
#define _ORE_H_

#include <stddef.h>
#include <stdint.h>

typedef enum {
    TOK_EOF,
    TOK_IDENT,
    TOK_INT,
    TOK_STRING,

    TOK_AS,
    TOK_BREAK,
    TOK_CASE,
    TOK_CONST,
    TOK_CONTINUE,
    TOK_DEFAULT,
    TOK_ELSE,
    TOK_ENUM,
    TOK_EXTERN,
    TOK_FALSE,
    TOK_FN,
    TOK_FOR,
    TOK_IF,
    TOK_IMPORT,
    TOK_INTERRUPT,
    TOK_NULL,
    TOK_PUB,
    TOK_RETURN,
    TOK_STRUCT,
    TOK_SWITCH,
    TOK_TRUE,
    TOK_VAR,
    TOK_WHILE,

    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_COMMA,
    TOK_SEMI,
    TOK_COLON,
    TOK_DOT,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_AMP,
    TOK_PIPE,
    TOK_CARET,
    TOK_TILDE,
    TOK_BANG,
    TOK_SHL,
    TOK_SHR,
    TOK_ANDAND,
    TOK_OROR,
    TOK_EQ,
    TOK_NE,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE,
    TOK_ASSIGN,
    TOK_ADD_ASSIGN,
    TOK_SUB_ASSIGN,
    TOK_MUL_ASSIGN,
    TOK_DIV_ASSIGN,
    TOK_MOD_ASSIGN,
    TOK_AND_ASSIGN,
    TOK_OR_ASSIGN,
    TOK_XOR_ASSIGN,
    TOK_SHL_ASSIGN,
    TOK_SHR_ASSIGN,
} TokenKind;

typedef struct {
    TokenKind kind;
    int line;
    const char *text;       // where the token starts in the source
    int length;
    int64_t value;          // integer and character literals
    char *bytes;            // string literals, decoded and NUL-terminated
    int size;
} Token;

typedef struct Module Module;
typedef struct Type Type;
typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Decl Decl;
typedef struct Symbol Symbol;

typedef enum { TE_NAME, TE_POINTER, TE_ARRAY, TE_SLICE, TE_FN } TypeExprKind;

typedef struct TypeExpr {
    TypeExprKind kind;
    int line;
    const char *qualifier;  // the module in geometry.Point
    const char *name;
    struct TypeExpr *base;  // element type, or the result of a function
    Expr *length;           // of an array
    struct TypeExpr **params;
    int param_count;
} TypeExpr;

typedef enum {
    EX_INT,
    EX_BOOL,
    EX_NULL,
    EX_STRING,
    EX_NAME,
    EX_UNARY,
    EX_BINARY,
    EX_CALL,
    EX_INDEX,
    EX_SLICE,
    EX_FIELD,
    EX_CAST,
    EX_STRUCT,
    EX_ARRAY,
    EX_SIZEOF,
    EX_NEW,
} ExprKind;

struct Expr {
    ExprKind kind;
    int line;
    TokenKind op;
    int64_t value;          // literals
    char *bytes;            // string literals
    int size;
    const char *name;       // names and fields
    Expr *left, *right, *third;
    Expr **args;            // call arguments and literal elements
    const char **fields;    // field names of a struct literal, one per argument
    int arg_count;
    TypeExpr *type_expr;    // casts, literals, sizeof and new
};

typedef struct {
    Expr **values;
    int value_count;
    Stmt *body;
    int line;
} Case;

typedef enum {
    ST_BLOCK,
    ST_VAR,
    ST_CONST,
    ST_ASSIGN,
    ST_EXPR,
    ST_IF,
    ST_WHILE,
    ST_FOR,
    ST_SWITCH,
    ST_BREAK,
    ST_CONTINUE,
    ST_RETURN,
} StmtKind;

struct Stmt {
    StmtKind kind;
    int line;
    int end_line;           // closing brace of a block
    Stmt **body;
    int count;
    const char *name;       // variables and constants
    TypeExpr *type_expr;
    Expr *value;            // initializer, assigned or returned value, expression statement
    Expr *target;           // assigned place
    TokenKind op;           // TOK_ASSIGN or a compound assignment
    Expr *cond;
    Stmt *then, *otherwise; // if; the body of while and for
    Stmt *init, *step;      // for
    Case *cases;
    int case_count;
    Stmt *fallback;         // default block of a switch
};

typedef struct {
    const char *name;
    TypeExpr *type_expr;
    int line;
} Param;

typedef struct {
    const char *name;
    Expr *value;
    int line;
} Member;

typedef enum { DECL_IMPORT, DECL_CONST, DECL_VAR, DECL_FN, DECL_STRUCT, DECL_ENUM } DeclKind;

struct Decl {
    DeclKind kind;
    int line;
    int is_pub;
    int is_extern;
    int is_interrupt;
    const char *name;
    Module *module;
    TypeExpr *type_expr;    // declared type of a constant or variable, result of a function
    Expr *value;            // initializer
    Param *params;          // function parameters and struct fields
    int param_count;
    Stmt *body;
    Member *members;        // enum
    int member_count;
    const char *path;       // import
    Module *imported;
};

struct Module {
    const char *path;
    const char *name;
    char *source;
    const char **lines;     // start of each source line, for .loc
    int line_count;
    Token *tokens;
    int token_count;
    Decl **decls;
    int decl_count;
};

typedef struct {
    Module **modules;       // the main module first
    int module_count;
    const char **include_dirs;
    int include_dir_count;
    const char *library;
} Program;

void *allocate(size_t size);
char *copy_text(const char *text, size_t length);
void grow(void **items, int count, size_t item_size);
_Noreturn void fail(const Module *m, int line, const char *format, ...);

void lex(Module *m);
const char *token_text(TokenKind kind);
void parse(Program *program, Module *m);
Module *load_module(Program *program, const char *path, const Module *importer, int line);

#endif // _ORE_H_
