#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asm.h"
#include "binfmt.h"
#include "buffer.h"

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

// Symbols in definition order, then one line record for every statement that emitted bytes
static void write_debug_info(Assembler *as, Buffer *b) {
    buffer_u32(b, (uint32_t)as->symbols.count);
    for (size_t i = 0; i < as->symbols.count; i++) {
        const AsmSymbol *sym = &as->symbols.items[i];
        buffer_string(b, sym->name);
        buffer_u32(b, (uint32_t)sym->value);
        buffer_u8(b, (uint8_t)sym->kind);
        buffer_u32(b, sym->line ? (uint32_t)sym->line->number : 0);
        buffer_string(b, sym->line ? sym->line->file : NULL);
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
            buffer_u32(b, r->address);
            buffer_u32(b, (uint32_t)as->lines[i].number);
            buffer_string(b, trim(as->lines[i].text, text, sizeof(text)));
            buffer_string(b, as->lines[i].file);
        }
    }
}

int output_binary(Assembler *as, const char *path, int with_debug) {
    const Section *code = &as->sections[SECTION_TEXT];
    const Section *data = &as->sections[SECTION_DATA];
    uint32_t code_size = code->end - code->base;
    uint32_t data_size = data->end - data->base;
    Buffer b = { 0 };

    buffer_put(&b, VM32_MAGIC, 4);
    buffer_u16(&b, VM32_VERSION_MAJOR);
    buffer_u16(&b, VM32_VERSION_MINOR);
    buffer_u32(&b, VM32_HEADER_SIZE);
    buffer_u32(&b, code->base);
    buffer_u32(&b, code_size);
    buffer_u32(&b, data->base);
    buffer_u32(&b, data_size);
    buffer_u32(&b, 0);
    buffer_u32(&b, (uint32_t)as->entry);
    buffer_put(&b, code->bytes, code_size);
    buffer_put(&b, data->bytes, data_size);

    size_t symbols_start = b.size;
    if (with_debug) {
        write_debug_info(as, &b);
    }
    if (!b.failed) {
        write_le32(b.data + 28, (uint32_t)(b.size - symbols_start));
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
