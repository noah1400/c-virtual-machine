#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asm.h"
#include "binfmt.h"
#include "buffer.h"
#include "objfmt.h"

static const char *trim(const char *text, char *out, size_t size) {
    while (*text == ' ' || *text == '\t') {
        text++;
    }
    snprintf(out, size, "%s", text);
    size_t length = strlen(out);
    while (length > 0 && (out[length - 1] == ' ' || out[length - 1] == '\t')) {
        out[--length] = '\0';
    }
    return out;
}

typedef struct {
    const char *file;
    uint32_t number;
    const char *text;
} Origin;

// The line that debug information names for a line of assembly: the line of another source that a .loc
// before it named, or the line itself
static Origin origin(const Assembler *as, const SourceLine *line) {
    size_t location = line ? as->results[line - as->lines].location : 0;
    if (location) {
        const Location *loc = &as->locations[location - 1];
        return (Origin){ loc->file, (uint32_t)loc->line, loc->text };
    }
    return line ? (Origin){ line->file, (uint32_t)line->number, line->text } : (Origin){ NULL, 0, "" };
}

// Symbols in definition order, then one line record for every statement that emitted bytes
static void write_debug_info(Assembler *as, Buffer *b) {
    buffer_u32(b, (uint32_t)as->symbols.count);
    for (size_t i = 0; i < as->symbols.count; i++) {
        const AsmSymbol *sym = &as->symbols.items[i];
        Origin o = origin(as, sym->line);
        vm32_write_symbol(b, sym->name, (uint32_t)sym->value, (uint8_t)sym->kind, o.number, o.file);
    }

    uint32_t count = 0;
    for (size_t i = 0; i < as->line_count; i++) {
        count += as->results[i].section >= 0;
    }
    buffer_u32(b, count);
    for (size_t i = 0; i < as->line_count; i++) {
        const LineResult *r = &as->results[i];
        if (r->section >= 0) {
            char text[1024];
            Origin o = origin(as, &as->lines[i]);
            vm32_write_line(b, r->address, o.number, trim(o.text, text, sizeof(text)), o.file);
        }
    }

    buffer_u32(b, (uint32_t)as->inlined_count);
    for (size_t i = 0; i < as->inlined_count; i++) {
        const Inlined *copy = &as->inlined[i];
        vm32_write_inlined(b, copy->start, copy->end, (uint32_t)copy->line, copy->file);
    }
}

int output_binary(Assembler *as, const char *path, int with_debug) {
    const Section *code = &as->sections[SECTION_TEXT];
    const Section *data = &as->sections[SECTION_DATA];
    uint32_t code_size = code->end - code->base;
    uint32_t data_size = data->end - data->base;
    Buffer b = { 0 };

    vm32_write_header(&b, code->base, code_size, data->base, data_size, (uint32_t)as->entry);
    buffer_put(&b, code->bytes, code_size);
    buffer_put(&b, data->bytes, data_size);

    size_t symbols_start = b.size;
    if (with_debug) {
        write_debug_info(as, &b);
    }
    vm32_finish(&b, symbols_start);

    int ok = buffer_save(&b, path);
    if (!ok) {
        fprintf(stderr, "vmasm: error: cannot write %s\n", path);
    }
    free(b.data);
    return ok;
}

static uint8_t object_kind(const AsmSymbol *sym) {
    if (sym->external) {
        return VMO_EXTERN;
    }
    if (sym->base == BASE_TEXT) {
        return VMO_CODE;
    }
    return sym->base == BASE_DATA ? VMO_DATA_SYMBOL : VMO_CONST;
}

static uint32_t relocation_target(int base) {
    return base == BASE_TEXT ? VMO_TARGET_TEXT : base == BASE_DATA ? VMO_TARGET_DATA : VMO_SYMBOL + (uint32_t)(base - BASE_SYMBOL);
}

int output_object(Assembler *as, const char *path) {
    const Section *text = &as->sections[SECTION_TEXT];
    const Section *data = &as->sections[SECTION_DATA];
    uint32_t text_size = text->end - text->base;
    uint32_t data_size = data->end - data->base;
    Buffer b = { 0 };

    buffer_put(&b, VMO_MAGIC, 4);
    buffer_u16(&b, VMO_VERSION);
    buffer_u16(&b, as->entry_line ? VMO_HAS_ENTRY : 0);
    buffer_u32(&b, as->entry_line ? (uint32_t)as->entry : 0);
    buffer_u32(&b, text_size);
    buffer_u32(&b, text->alignment > 4 ? text->alignment : 4);
    buffer_u32(&b, data_size);
    buffer_u32(&b, data->alignment > 4 ? data->alignment : 4);
    buffer_put(&b, text->bytes, text_size);
    buffer_put(&b, data->bytes, data_size);

    // Constants that alias an external symbol only matter inside this file
    buffer_u32(&b, (uint32_t)as->symbols.count);
    for (size_t i = 0; i < as->symbols.count; i++) {
        const AsmSymbol *sym = &as->symbols.items[i];
        int alias = sym->base >= BASE_SYMBOL && !sym->external;
        buffer_string(&b, sym->name);
        buffer_u8(&b, alias ? VMO_CONST : object_kind(sym));
        buffer_u8(&b, (uint8_t)sym->global);
        buffer_u32(&b, alias ? 0 : (uint32_t)sym->value);
        Origin o = origin(as, sym->line);
        buffer_u32(&b, o.number);
        buffer_string(&b, o.file);
    }

    buffer_u32(&b, (uint32_t)as->relocation_count);
    for (size_t i = 0; i < as->relocation_count; i++) {
        const Relocation *r = &as->relocations[i];
        buffer_u8(&b, r->section == SECTION_TEXT ? VMO_TEXT : VMO_DATA);
        buffer_u8(&b, (uint8_t)r->type);
        buffer_u32(&b, r->offset);
        buffer_u32(&b, relocation_target(r->base));
        buffer_u32(&b, (uint32_t)r->addend);
    }

    uint32_t count = 0;
    for (size_t i = 0; i < as->line_count; i++) {
        count += as->results[i].section >= 0;
    }
    buffer_u32(&b, count);
    for (size_t i = 0; i < as->line_count; i++) {
        const LineResult *r = &as->results[i];
        if (r->section >= 0) {
            char line_text[1024];
            Origin o = origin(as, &as->lines[i]);
            buffer_u8(&b, r->section == SECTION_TEXT ? VMO_TEXT : VMO_DATA);
            buffer_u32(&b, r->address);
            buffer_u32(&b, o.number);
            buffer_string(&b, trim(o.text, line_text, sizeof(line_text)));
            buffer_string(&b, o.file);
        }
    }

    buffer_u32(&b, (uint32_t)as->inlined_count);
    for (size_t i = 0; i < as->inlined_count; i++) {
        const Inlined *copy = &as->inlined[i];
        buffer_u8(&b, copy->section == SECTION_TEXT ? VMO_TEXT : VMO_DATA);
        buffer_u32(&b, copy->start);
        buffer_u32(&b, copy->end);
        buffer_u32(&b, (uint32_t)copy->line);
        buffer_string(&b, copy->file);
    }

    int ok = buffer_save(&b, path);
    if (!ok) {
        fprintf(stderr, "vmasm: error: cannot write %s\n", path);
    }
    free(b.data);
    return ok;
}

int output_listing(Assembler *as, const char *path) {
    FILE *file = fopen(path, "w");
    if (!file) {
        fprintf(stderr, "vmasm: error: cannot write %s\n", path);
        return 0;
    }

    const char *current_file = NULL;
    for (size_t i = 0; i < as->line_count; i++) {
        const SourceLine *line = &as->lines[i];
        const LineResult *r = &as->results[i];
        char bytes[64] = "";

        if (line->kind == LINE_INCLUDE_BEGIN || line->kind == LINE_INCLUDE_END) {
            continue;
        }
        if (line->file != current_file) {
            fprintf(file, "%s%s:\n", current_file ? "\n" : "", line->file);
            current_file = line->file;
        }
        char marker = line->expanded ? '+' : ' ';
        if (r->section < 0) {
            fprintf(file, "%5d%c                             %s\n", line->number, marker, line->text);
            continue;
        }

        const Section *sec = &as->sections[r->section];
        const uint8_t *p = sec->bytes + (r->address - sec->base);
        size_t used = 0;
        if (r->is_code) {
            for (uint32_t offset = 0; offset + 4 <= r->size && offset < 8; offset += 4) {
                used += (size_t)snprintf(bytes + used, sizeof(bytes) - used, "%08X ", read_le32(p + offset));
            }
        } else {
            for (uint32_t offset = 0; offset < r->size && offset < 8; offset++) {
                used += (size_t)snprintf(bytes + used, sizeof(bytes) - used, "%02X", p[offset]);
            }
            if (r->size > 8) {
                snprintf(bytes + used, sizeof(bytes) - used, "...");
            }
        }
        fprintf(file, "%5d%c %04X  %-20s  %s\n", line->number, marker, r->address, bytes, line->text);
    }

    int ok = fclose(file) == 0;
    if (!ok) {
        fprintf(stderr, "vmasm: error: cannot write %s\n", path);
    }
    return ok;
}

static const AsmSymbol *sort_table;

static int compare_symbols(const void *a, const void *b) {
    const AsmSymbol *x = &sort_table[*(const size_t *)a];
    const AsmSymbol *y = &sort_table[*(const size_t *)b];
    if (x->value != y->value) {
        return x->value < y->value ? -1 : 1;
    }
    return strcmp(x->name, y->name);
}

void output_symbols(Assembler *as) {
    static const char *const kinds[] = { "code", "data", "const" };
    size_t *order = malloc(as->symbols.count * sizeof(size_t) + 1);
    if (!order) {
        return;
    }
    for (size_t i = 0; i < as->symbols.count; i++) {
        order[i] = i;
    }
    sort_table = as->symbols.items;
    qsort(order, as->symbols.count, sizeof(size_t), compare_symbols);

    for (size_t i = 0; i < as->symbols.count; i++) {
        const AsmSymbol *sym = &as->symbols.items[order[i]];
        printf("0x%08X  %-5s  %s\n", (uint32_t)sym->value, kinds[sym->kind], sym->name);
    }
    free(order);
}
