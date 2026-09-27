#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "buffer.h"
#include "debug.h"

static void parse_lines(DebugInfo *info, Reader *r, uint32_t size) {
    uint32_t line_count = reader_uint(r, 4);
    if (!r->ok || line_count > size / 12) {
        return;
    }
    info->source_lines = calloc(line_count ? line_count : 1, sizeof(SourceLine));
    if (!info->source_lines) {
        return;
    }
    for (uint32_t i = 0; i < line_count; i++) {
        SourceLine *line = &info->source_lines[i];
        line->address = reader_uint(r, 4);
        line->line_num = reader_uint(r, 4);
        line->source = reader_string(r);
        line->source_file = reader_string(r);
        if (!r->ok) {
            free(line->source);
            free(line->source_file);
            return;
        }
        info->source_line_count = i + 1;
    }
}

static int compare_addresses(const void *a, const void *b) {
    const SourceLine *x = *(const SourceLine *const *)a;
    const SourceLine *y = *(const SourceLine *const *)b;
    if (x->address != y->address) {
        return x->address < y->address ? -1 : 1;
    }
    return x < y ? -1 : x > y;
}

// Lines at the same address keep the order of the table
static void index_lines(DebugInfo *info) {
    info->lines_by_address = malloc((info->source_line_count ? info->source_line_count : 1) * sizeof(SourceLine *));
    if (!info->lines_by_address) {
        return;
    }
    for (uint32_t i = 0; i < info->source_line_count; i++) {
        info->lines_by_address[i] = &info->source_lines[i];
    }
    qsort(info->lines_by_address, info->source_line_count, sizeof(SourceLine *), compare_addresses);
}

DebugInfo *debug_info_parse(const uint8_t *data, uint32_t size) {
    DebugInfo *info = calloc(1, sizeof(DebugInfo));
    if (!info || !data) {
        return info;
    }

    Reader r = { data, data + size, 1 };

    // Entries have a minimum encoded size, which bounds allocations for corrupt counts
    uint32_t symbol_count = reader_uint(&r, 4);
    if (!r.ok || symbol_count > size / 13) {
        return info;
    }
    info->symbols = calloc(symbol_count ? symbol_count : 1, sizeof(Symbol));
    if (!info->symbols) {
        return info;
    }
    for (uint32_t i = 0; i < symbol_count; i++) {
        Symbol *sym = &info->symbols[i];
        sym->name = reader_string(&r);
        sym->address = reader_uint(&r, 4);
        sym->type = (uint8_t)reader_uint(&r, 1);
        sym->line_num = reader_uint(&r, 4);
        sym->source_file = reader_string(&r);
        if (!r.ok || !sym->name) {
            free(sym->name);
            free(sym->source_file);
            return info;
        }
        info->symbol_count = i + 1;
    }

    parse_lines(info, &r, size);
    index_lines(info);
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
    free(info->lines_by_address);
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

// Line that produced the address, or the closest line before it; the first of several at one address
const SourceLine *debug_line_at(const DebugInfo *info, uint32_t address) {
    if (!info || !info->lines_by_address) {
        return NULL;
    }
    const SourceLine *const *lines = info->lines_by_address;
    uint32_t low = 0, high = info->source_line_count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2;
        if (lines[middle]->address <= address) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    if (low == 0) {
        return NULL;
    }
    uint32_t found = low - 1;
    while (found > 0 && lines[found - 1]->address == lines[found]->address) {
        found--;
    }
    return lines[found];
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
