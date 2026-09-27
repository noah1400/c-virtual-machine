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

typedef struct DebugInfo {
    Symbol *symbols;
    uint32_t symbol_count;
    SourceLine *source_lines;
    uint32_t source_line_count;
    const SourceLine **lines_by_address;
} DebugInfo;

// Parses the symbol table section of a VM32 binary; truncated tables yield the entries read so far
DebugInfo *debug_info_parse(const uint8_t *data, uint32_t size);
void debug_info_free(DebugInfo *info);

const Symbol *debug_symbol_at(const DebugInfo *info, uint32_t address);
const Symbol *debug_symbol_near(const DebugInfo *info, uint32_t address);
const Symbol *debug_symbol_named(const DebugInfo *info, const char *name);
const SourceLine *debug_line_at(const DebugInfo *info, uint32_t address);

// Writes "<label>" or "<label+offset>" for the closest label at or before the address, or ""
void debug_describe(const DebugInfo *info, uint32_t address, char *out, size_t size);

#endif // _DEBUG_H_
