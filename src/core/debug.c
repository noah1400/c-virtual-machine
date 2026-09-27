#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"

typedef struct {
    const uint8_t *ptr;
    const uint8_t *end;
    bool ok;
} Reader;

static uint32_t read_uint(Reader *r, int bytes) {
    if (!r->ok || r->end - r->ptr < bytes) {
        r->ok = false;
        return 0;
    }
    uint32_t value = 0;
    for (int i = 0; i < bytes; i++) {
        value |= (uint32_t)r->ptr[i] << (8 * i);
    }
    r->ptr += bytes;
    return value;
}

// Reads a length-prefixed string; empty strings become NULL
static char *read_string(Reader *r) {
    uint16_t len = (uint16_t)read_uint(r, 2);
    if (!r->ok || r->end - r->ptr < len) {
        r->ok = false;
        return NULL;
    }
    if (len == 0) {
        return NULL;
    }
    char *str = malloc(len + 1u);
    if (!str) {
        r->ok = false;
        return NULL;
    }
    memcpy(str, r->ptr, len);
    str[len] = '\0';
    r->ptr += len;
    return str;
}

DebugInfo *debug_info_parse(const uint8_t *data, uint32_t size) {
    DebugInfo *info = calloc(1, sizeof(DebugInfo));
    if (!info || !data) {
        return info;
    }

    Reader r = { data, data + size, true };

    // Entries have a minimum encoded size, which bounds allocations for corrupt counts
    uint32_t symbol_count = read_uint(&r, 4);
    if (!r.ok || symbol_count > size / 13) {
        return info;
    }
    info->symbols = calloc(symbol_count ? symbol_count : 1, sizeof(Symbol));
    if (!info->symbols) {
        return info;
    }
    for (uint32_t i = 0; i < symbol_count; i++) {
        Symbol *sym = &info->symbols[i];
        sym->name = read_string(&r);
        sym->address = read_uint(&r, 4);
        sym->type = (uint8_t)read_uint(&r, 1);
        sym->line_num = read_uint(&r, 4);
        sym->source_file = read_string(&r);
        if (!r.ok || !sym->name) {
            free(sym->name);
            free(sym->source_file);
            return info;
        }
        info->symbol_count = i + 1;
    }

    uint32_t line_count = read_uint(&r, 4);
    if (!r.ok || line_count > size / 12) {
        return info;
    }
    info->source_lines = calloc(line_count ? line_count : 1, sizeof(SourceLine));
    if (!info->source_lines) {
        return info;
    }
    for (uint32_t i = 0; i < line_count; i++) {
        SourceLine *line = &info->source_lines[i];
        line->address = read_uint(&r, 4);
        line->line_num = read_uint(&r, 4);
        line->source = read_string(&r);
        line->source_file = read_string(&r);
        if (!r.ok) {
            free(line->source);
            free(line->source_file);
            return info;
        }
        info->source_line_count = i + 1;
    }

    return info;
}

void debug_info_free(DebugInfo *info) {
    if (!info) {
        return;
    }
    for (uint32_t i = 0; i < info->symbol_count; i++) {
        free(info->symbols[i].name);
        free(info->symbols[i].source_file);
    }
    for (uint32_t i = 0; i < info->source_line_count; i++) {
        free(info->source_lines[i].source);
        free(info->source_lines[i].source_file);
    }
    free(info->symbols);
    free(info->source_lines);
    free(info);
}

const Symbol *debug_symbol_at(const DebugInfo *info, uint32_t address) {
    for (uint32_t i = 0; info && i < info->symbol_count; i++) {
        if (info->symbols[i].type != SYMBOL_CONST && info->symbols[i].address == address) {
            return &info->symbols[i];
        }
    }
    return NULL;
}

// Closest label at or before the address
const Symbol *debug_symbol_near(const DebugInfo *info, uint32_t address) {
    const Symbol *best = NULL;
    for (uint32_t i = 0; info && i < info->symbol_count; i++) {
        const Symbol *sym = &info->symbols[i];
        if (sym->type != SYMBOL_CONST && sym->address <= address &&
            (!best || sym->address > best->address)) {
            best = sym;
        }
    }
    return best;
}

const Symbol *debug_symbol_named(const DebugInfo *info, const char *name) {
    for (uint32_t i = 0; info && i < info->symbol_count; i++) {
        if (strcmp(info->symbols[i].name, name) == 0) {
            return &info->symbols[i];
        }
    }
    return NULL;
}

// Line that produced the address, or the closest line before it
const SourceLine *debug_line_at(const DebugInfo *info, uint32_t address) {
    const SourceLine *best = NULL;
    for (uint32_t i = 0; info && i < info->source_line_count; i++) {
        const SourceLine *line = &info->source_lines[i];
        if (line->address <= address && (!best || line->address > best->address)) {
            best = line;
        }
    }
    return best;
}

void debug_describe(const DebugInfo *info, uint32_t address, char *out, size_t size) {
    const Symbol *sym = debug_symbol_near(info, address);
    if (!sym) {
        out[0] = '\0';
    } else if (sym->address == address) {
        snprintf(out, size, "<%s>", sym->name);
    } else {
        snprintf(out, size, "<%s+%u>", sym->name, address - sym->address);
    }
}
