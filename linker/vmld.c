#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "binfmt.h"
#include "buffer.h"
#include "objfmt.h"
#include "vm_types.h"

typedef struct {
    char *name;
    uint8_t kind;
    uint8_t global;
    uint32_t value;
    uint32_t line;
    char *file;
} ObjSymbol;

typedef struct {
    uint8_t section, type;
    uint32_t offset, target, addend;
} ObjRelocation;

typedef struct {
    uint8_t section;
    uint32_t offset, line;
    char *text, *file;
} ObjLine;

typedef struct {
    const char *path;
    uint8_t *bytes;
    int has_entry;
    uint32_t entry;
    uint32_t sizes[2], alignments[2], bases[2];
    const uint8_t *contents[2];
    ObjSymbol *symbols;
    uint32_t symbol_count;
    ObjRelocation *relocations;
    uint32_t relocation_count;
    ObjLine *lines;
    uint32_t line_count;
} Object;

typedef struct {
    Object *objects;
    int count;
    int errors;
    uint32_t base;      // where the code starts
} Linker;

static void link_error(Linker *ld, const char *path, const char *format, const char *detail) {
    fprintf(stderr, "vmld: error: ");
    if (path) {
        fprintf(stderr, "%s: ", path);
    }
    fprintf(stderr, format, detail);
    fputc('\n', stderr);
    ld->errors++;
}

static void free_object(Object *o) {
    for (uint32_t i = 0; o->symbols && i < o->symbol_count; i++) {
        free(o->symbols[i].name);
        free(o->symbols[i].file);
    }
    for (uint32_t i = 0; o->lines && i < o->line_count; i++) {
        free(o->lines[i].text);
        free(o->lines[i].file);
    }
    free(o->symbols);
    free(o->relocations);
    free(o->lines);
    free(o->bytes);
}

// Reads a count and checks that that many entries of at least min_size bytes can follow
static uint32_t read_count(Reader *r, uint32_t min_size) {
    uint32_t count = reader_uint(r, 4);
    if (r->ok && count > (uint32_t)(r->end - r->ptr) / min_size) {
        r->ok = 0;
    }
    return r->ok ? count : 0;
}

// Returns NULL, or what is wrong with the file
static const char *parse_object(Object *o, uint8_t *bytes, uint32_t size) {
    Reader r = { bytes, bytes + size, 1 };

    o->bytes = bytes;
    if (size < 4 || memcmp(bytes, VMO_MAGIC, 4) != 0) {
        return "not a VM32 object file";
    }
    r.ptr += 4;
    if (reader_uint(&r, 2) != VMO_VERSION) {
        return "unsupported object file version";
    }
    o->has_entry = (reader_uint(&r, 2) & VMO_HAS_ENTRY) != 0;
    o->entry = reader_uint(&r, 4);
    for (int s = 0; s < 2; s++) {
        o->sizes[s] = reader_uint(&r, 4);
        o->alignments[s] = reader_uint(&r, 4);
        if (o->alignments[s] == 0 || o->alignments[s] > VM_PAGE_SIZE || (o->alignments[s] & (o->alignments[s] - 1))) {
            return "invalid section alignment";
        }
    }
    for (int s = 0; s < 2 && r.ok; s++) {
        if ((uint32_t)(r.end - r.ptr) < o->sizes[s]) {
            return "truncated section";
        }
        o->contents[s] = r.ptr;
        r.ptr += o->sizes[s];
    }

    o->symbol_count = read_count(&r, 14);
    o->symbols = calloc(o->symbol_count ? o->symbol_count : 1, sizeof(ObjSymbol));
    for (uint32_t i = 0; o->symbols && r.ok && i < o->symbol_count; i++) {
        ObjSymbol *sym = &o->symbols[i];
        sym->name = reader_string(&r);
        sym->kind = (uint8_t)reader_uint(&r, 1);
        sym->global = (uint8_t)reader_uint(&r, 1);
        sym->value = reader_uint(&r, 4);
        sym->line = reader_uint(&r, 4);
        sym->file = reader_string(&r);
        if (r.ok && (!sym->name || sym->kind > VMO_EXTERN ||
                     (sym->kind < VMO_CONST && sym->value > o->sizes[sym->kind == VMO_CODE ? VMO_TEXT : VMO_DATA]))) {
            return "invalid symbol";
        }
    }

    o->relocation_count = read_count(&r, 14);
    o->relocations = calloc(o->relocation_count ? o->relocation_count : 1, sizeof(ObjRelocation));
    for (uint32_t i = 0; o->relocations && r.ok && i < o->relocation_count; i++) {
        ObjRelocation *rel = &o->relocations[i];
        rel->section = (uint8_t)reader_uint(&r, 1);
        rel->type = (uint8_t)reader_uint(&r, 1);
        rel->offset = reader_uint(&r, 4);
        rel->target = reader_uint(&r, 4);
        rel->addend = reader_uint(&r, 4);
        if (r.ok && (rel->section > VMO_DATA || rel->type > VMO_REL32 || o->sizes[rel->section] < 4 ||
                     rel->offset > o->sizes[rel->section] - 4 ||
                     (rel->target >= VMO_SYMBOL && rel->target - VMO_SYMBOL >= o->symbol_count))) {
            return "invalid relocation";
        }
    }

    o->line_count = read_count(&r, 13);
    o->lines = calloc(o->line_count ? o->line_count : 1, sizeof(ObjLine));
    for (uint32_t i = 0; o->lines && r.ok && i < o->line_count; i++) {
        ObjLine *line = &o->lines[i];
        line->section = (uint8_t)reader_uint(&r, 1);
        line->offset = reader_uint(&r, 4);
        line->line = reader_uint(&r, 4);
        line->text = reader_string(&r);
        line->file = reader_string(&r);
        if (r.ok && line->section > VMO_DATA) {
            return "invalid source line";
        }
    }

    if (!o->symbols || !o->relocations || !o->lines) {
        return "out of memory";
    }
    return r.ok ? NULL : "truncated object file";
}

static uint32_t align_up(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

// Code starts at the base and data on the page after it, each object file's sections in command line order
static uint32_t place_sections(Linker *ld, uint32_t *code_end, uint32_t *data_start) {
    uint64_t address = ld->base;
    for (int s = 0; s < 2; s++) {
        if (s == VMO_DATA) {
            *code_end = (uint32_t)address;
            address = align_up((uint32_t)address, VM_PAGE_SIZE);
            *data_start = (uint32_t)address;
        }
        for (int i = 0; i < ld->count; i++) {
            Object *o = &ld->objects[i];
            address = align_up((uint32_t)address, o->alignments[s]);
            o->bases[s] = (uint32_t)address;
            address += o->sizes[s];
            if (address - ld->base > VM32_MAX_FILE_SIZE) {
                link_error(ld, NULL, "the program exceeds %s", "16 MB");
                return 0;
            }
        }
    }
    return (uint32_t)address;
}

static uint32_t symbol_address(const Object *o, const ObjSymbol *sym) {
    switch (sym->kind) {
        case VMO_CODE:
            return o->bases[VMO_TEXT] + sym->value;
        case VMO_DATA_SYMBOL:
            return o->bases[VMO_DATA] + sym->value;
        default:
            return sym->value;
    }
}

// Finds the one object file that exports a name
static const ObjSymbol *find_global(const Linker *ld, const char *name, const Object **owner) {
    for (int i = 0; i < ld->count; i++) {
        const Object *o = &ld->objects[i];
        for (uint32_t k = 0; k < o->symbol_count; k++) {
            const ObjSymbol *sym = &o->symbols[k];
            if (sym->global && sym->kind != VMO_EXTERN && strcmp(sym->name, name) == 0) {
                *owner = o;
                return sym;
            }
        }
    }
    return NULL;
}

static void check_duplicates(Linker *ld) {
    for (int i = 0; i < ld->count; i++) {
        const Object *o = &ld->objects[i];
        for (uint32_t k = 0; k < o->symbol_count; k++) {
            const ObjSymbol *sym = &o->symbols[k];
            const Object *owner;
            if (sym->global && sym->kind != VMO_EXTERN && find_global(ld, sym->name, &owner) && owner != o) {
                char detail[512];
                snprintf(detail, sizeof(detail), "'%s' is also exported by %s", sym->name, owner->path);
                link_error(ld, o->path, "%s", detail);
            }
        }
    }
}

// Where a relocation points: a section of its own file, a symbol of it, or the export of another file
static int relocation_target(Linker *ld, const Object *o, const ObjRelocation *rel, uint32_t *address) {
    if (rel->target < VMO_SYMBOL) {
        *address = o->bases[rel->target == VMO_TARGET_TEXT ? VMO_TEXT : VMO_DATA];
        return 1;
    }
    const ObjSymbol *sym = &o->symbols[rel->target - VMO_SYMBOL];
    if (sym->kind != VMO_EXTERN) {
        *address = symbol_address(o, sym);
        return 1;
    }
    const Object *owner;
    const ObjSymbol *definition = find_global(ld, sym->name, &owner);
    if (!definition) {
        link_error(ld, o->path, "undefined symbol '%s'", sym->name);
        return 0;
    }
    *address = symbol_address(owner, definition);
    return 1;
}

static void apply_relocations(Linker *ld, uint8_t *image, uint32_t base_address) {
    for (int i = 0; i < ld->count; i++) {
        const Object *o = &ld->objects[i];
        for (uint32_t k = 0; k < o->relocation_count; k++) {
            const ObjRelocation *rel = &o->relocations[k];
            uint32_t site = o->bases[rel->section] + rel->offset, target;
            if (!relocation_target(ld, o, rel, &target)) {
                continue;
            }
            uint32_t value = target + rel->addend;
            if (rel->type == VMO_REL32) {
                value -= site + 4;
            }
            write_le32(image + (site - base_address), value);
        }
    }
}

// The entry point is a symbol given with -e, the .entry of one object file, or the start of the code
static int find_entry(Linker *ld, const char *name, uint32_t code_end, uint32_t *entry) {
    const Object *from = NULL;

    *entry = ld->base;
    if (name) {
        const Object *owner;
        const ObjSymbol *sym = find_global(ld, name, &owner);
        if (!sym || sym->kind != VMO_CODE) {
            link_error(ld, NULL, "the entry point '%s' is not exported code", name);
            return 0;
        }
        *entry = symbol_address(owner, sym);
    } else {
        for (int i = 0; i < ld->count; i++) {
            if (ld->objects[i].has_entry && from) {
                char detail[512];
                snprintf(detail, sizeof(detail), "%s and %s both set an entry point", from->path, ld->objects[i].path);
                link_error(ld, NULL, "%s", detail);
                return 0;
            }
            if (ld->objects[i].has_entry) {
                from = &ld->objects[i];
                *entry = from->bases[VMO_TEXT] + from->entry;
            }
        }
    }
    if (*entry >= code_end) {
        link_error(ld, NULL, "the program has no code at its entry point%s", "");
        return 0;
    }
    return 1;
}

static void write_debug_info(const Linker *ld, Buffer *b) {
    uint32_t symbols = 0, lines = 0;
    for (int i = 0; i < ld->count; i++) {
        for (uint32_t k = 0; k < ld->objects[i].symbol_count; k++) {
            symbols += ld->objects[i].symbols[k].kind != VMO_EXTERN;
        }
        lines += ld->objects[i].line_count;
    }

    buffer_u32(b, symbols);
    for (int i = 0; i < ld->count; i++) {
        const Object *o = &ld->objects[i];
        for (uint32_t k = 0; k < o->symbol_count; k++) {
            const ObjSymbol *sym = &o->symbols[k];
            if (sym->kind != VMO_EXTERN) {
                vm32_write_symbol(b, sym->name, symbol_address(o, sym), sym->kind, sym->line, sym->file);
            }
        }
    }
    buffer_u32(b, lines);
    for (int i = 0; i < ld->count; i++) {
        const Object *o = &ld->objects[i];
        for (uint32_t k = 0; k < o->line_count; k++) {
            const ObjLine *line = &o->lines[k];
            vm32_write_line(b, o->bases[line->section] + line->offset, line->line, line->text, line->file);
        }
    }
}

static void print_map(const Linker *ld, uint32_t entry) {
    printf("entry 0x%04X\n", entry);
    for (int i = 0; i < ld->count; i++) {
        const Object *o = &ld->objects[i];
        printf("%s: text 0x%04X-0x%04X, data 0x%04X-0x%04X\n", o->path, o->bases[VMO_TEXT],
               o->bases[VMO_TEXT] + o->sizes[VMO_TEXT], o->bases[VMO_DATA], o->bases[VMO_DATA] + o->sizes[VMO_DATA]);
        for (uint32_t k = 0; k < o->symbol_count; k++) {
            const ObjSymbol *sym = &o->symbols[k];
            if (sym->global && sym->kind != VMO_EXTERN) {
                printf("    0x%08X  %s\n", symbol_address(o, sym), sym->name);
            }
        }
    }
}

static int link_program(Linker *ld, const char *output, const char *entry_name, int with_debug, int map) {
    uint32_t code_end = 0, data_start = 0, entry;
    uint32_t end = place_sections(ld, &code_end, &data_start);

    check_duplicates(ld);
    if (ld->errors || !find_entry(ld, entry_name, code_end, &entry)) {
        return 0;
    }

    uint8_t *image = calloc(end - ld->base ? end - ld->base : 1, 1);
    if (!image) {
        link_error(ld, NULL, "out of memory%s", "");
        return 0;
    }
    for (int i = 0; i < ld->count; i++) {
        const Object *o = &ld->objects[i];
        for (int s = 0; s < 2; s++) {
            memcpy(image + (o->bases[s] - ld->base), o->contents[s], o->sizes[s]);
        }
    }
    apply_relocations(ld, image, ld->base);

    int ok = 0;
    if (ld->errors == 0) {
        Buffer b = { 0 };
        vm32_write_header(&b, ld->base, code_end - ld->base, data_start, end - data_start, entry);
        buffer_put(&b, image, code_end - ld->base);
        buffer_put(&b, image + (data_start - ld->base), end - data_start);
        size_t symbols_start = b.size;
        if (with_debug) {
            write_debug_info(ld, &b);
        }
        vm32_finish(&b, symbols_start);
        ok = buffer_save(&b, output);
        if (!ok) {
            link_error(ld, output, "cannot write the program%s", "");
        }
        free(b.data);
    }
    if (ok && map) {
        print_map(ld, entry);
    }
    free(image);
    return ok;
}

static void print_usage(FILE *out, const char *name) {
    fprintf(out, "Usage: %s [options] file.o...\n", name);
    fprintf(out, "Options:\n");
    fprintf(out, "  -o FILE   Write the program to FILE (default: the first object file with a .bin extension)\n");
    fprintf(out, "  -e NAME   Start at the exported code label NAME\n");
    fprintf(out, "  -b ADDR   Put the code at ADDR, a multiple of 4096 below 0x80000000, and the data after it\n");
    fprintf(out, "  -M        Print where the sections and exported symbols went\n");
    fprintf(out, "  -S        Leave out debug information\n");
    fprintf(out, "  -h        Show this help\n");
}

static char *default_output(const char *input) {
    const char *slash = strrchr(input, '/');
    const char *dot = strrchr(input, '.');
    size_t stem = dot && (!slash || dot > slash) ? (size_t)(dot - input) : strlen(input);
    char *path = malloc(stem + 5);
    if (path) {
        memcpy(path, input, stem);
        strcpy(path + stem, ".bin");
    }
    return path;
}

int main(int argc, char *argv[]) {
    const char *output = NULL, *entry = NULL;
    int with_debug = 1, map = 0;
    Linker ld = { 0 };

    ld.objects = calloc((size_t)argc, sizeof(Object));
    if (!ld.objects) {
        fprintf(stderr, "vmld: error: out of memory\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if ((strcmp(arg, "-o") == 0 || strcmp(arg, "-e") == 0) && i + 1 < argc) {
            *(arg[1] == 'o' ? &output : &entry) = argv[++i];
        } else if (strcmp(arg, "-b") == 0 && i + 1 < argc) {
            char *end;
            unsigned long address = strtoul(argv[++i], &end, 0);
            if (*argv[i] == '\0' || *end != '\0' || address % VM_PAGE_SIZE != 0 || address >= 0x80000000ul) {
                fprintf(stderr, "vmld: error: the base address must be a multiple of 4096 below 0x80000000\n");
                free(ld.objects);
                return 1;
            }
            ld.base = (uint32_t)address;
        } else if (strcmp(arg, "-M") == 0) {
            map = 1;
        } else if (strcmp(arg, "-S") == 0) {
            with_debug = 0;
        } else if (strcmp(arg, "-h") == 0) {
            print_usage(stdout, argv[0]);
            free(ld.objects);
            return 0;
        } else if (arg[0] == '-') {
            fprintf(stderr, "vmld: error: unknown option or missing value '%s'\n", arg);
            print_usage(stderr, argv[0]);
            free(ld.objects);
            return 1;
        } else {
            Object *o = &ld.objects[ld.count++];
            uint32_t size;
            const char *problem;
            uint8_t *bytes = read_binary_file(arg, &size, &problem);
            o->path = arg;
            problem = bytes ? parse_object(o, bytes, size) : problem;
            if (problem) {
                link_error(&ld, arg, "%s", problem);
            }
        }
    }
    if (ld.count == 0 && ld.errors == 0) {
        print_usage(stderr, argv[0]);
        free(ld.objects);
        return 1;
    }

    char *output_path = output ? NULL : default_output(ld.objects[0].path);
    int ok = ld.errors == 0 && link_program(&ld, output ? output : output_path, entry, with_debug, map);

    for (int i = 0; i < ld.count; i++) {
        free_object(&ld.objects[i]);
    }
    free(ld.objects);
    free(output_path);
    return ok ? 0 : 1;
}
