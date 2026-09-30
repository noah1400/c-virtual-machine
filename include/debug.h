#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <stddef.h>
#include <stdint.h>

#define SYMBOL_CODE  0
#define SYMBOL_DATA  1
#define SYMBOL_CONST 2

typedef struct {
    char *name;
    uint32_t address;
    uint8_t type;           // SYMBOL_CODE, SYMBOL_DATA or SYMBOL_CONST
    uint32_t line_num;
    char *source_file;
} Symbol;

typedef struct {
    uint32_t address;
    uint32_t line_num;
    char *source;
    char *source_file;
} SourceLine;

// Code that a compiler copied from a function into a call of it, which backtraces show as the call
typedef struct {
    uint32_t start, end;    // the addresses of the copy
    uint32_t line_num;      // the line of the call
    char *source_file;
} InlinedCall;

typedef struct DebugInfo {
    Symbol *symbols;
    uint32_t symbol_count;
    SourceLine *source_lines;
    uint32_t source_line_count;
    const SourceLine **lines_by_address;
    InlinedCall *inlined;
    uint32_t inlined_count;
} DebugInfo;

// Parses the symbol table section of a VM32 binary; truncated tables yield the entries read so far
DebugInfo *debug_info_parse(const uint8_t *data, uint32_t size);
void debug_info_free(DebugInfo *info);

const Symbol *debug_symbol_at(const DebugInfo *info, uint32_t address);
const Symbol *debug_symbol_near(const DebugInfo *info, uint32_t address);
const Symbol *debug_symbol_named(const DebugInfo *info, const char *name);
const SourceLine *debug_line_at(const DebugInfo *info, uint32_t address);

// Puts the inlined calls whose copies hold the address in calls, innermost first, and returns how many
uint32_t debug_inlined_at(const DebugInfo *info, uint32_t address, const InlinedCall **calls, uint32_t max);

// Writes "<label>" or "<label+offset>" for the closest label at or before the address, or ""
void debug_describe(const DebugInfo *info, uint32_t address, char *out, size_t size);

#endif // _DEBUG_H_
