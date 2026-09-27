#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "binfmt.h"
#include "debug.h"
#include "disassembler.h"
#include "memory.h"
#include "monitor.h"
#include "vm.h"

#define LOG_STRING 64

// Prints the address, its label and the instruction there the way a trace shows them, padding the
// instruction to width columns
static void print_instruction(const VM *vm, uint32_t pc, int width) {
    Instruction instr;
    char where[80], text[160] = "?";

    if (vm_peek_instruction(vm, pc, &instr)) {
        disasm_format(&instr, pc, vm->debug_info, text, sizeof(text));
    }
    debug_describe(vm->debug_info, pc, where, sizeof(where));
    fprintf(stderr, "0x%04X %-20s %-*s", pc, where, width, text);
}

static void record_history(const VM *vm, Monitor *monitor, const uint32_t *before) {
    HistoryEntry *entry = &monitor->history[monitor->history_count++ % monitor->history_size];
    entry->pc = vm->error_pc;
    entry->exception = vm->exception;
    entry->changed = 0;
    memcpy(entry->registers, vm->registers, sizeof(entry->registers));
    for (int i = 0; i < 16; i++) {
        if (i != R3_PC && before[i] != vm->registers[i]) {
            entry->changed |= (uint16_t)(1u << i);
        }
    }
}

// Prints up to LOG_STRING characters of the NUL-terminated string at address, escaping the rest
static void print_string(const VM *vm, uint32_t address) {
    char bytes[LOG_STRING];
    uint32_t length = memory_peek(vm, address, bytes, sizeof(bytes));
    fputc('"', stderr);
    for (uint32_t i = 0; i < length && bytes[i]; i++) {
        unsigned char c = (unsigned char)bytes[i];
        if (c == '"' || c == '\\') {
            fprintf(stderr, "\\%c", c);
        } else if (c == '\n') {
            fprintf(stderr, "\\n");
        } else if (c >= 32 && c < 127) {
            fputc(c, stderr);
        } else {
            fprintf(stderr, "\\x%02X", c);
        }
    }
    fputc('"', stderr);
    if (length == sizeof(bytes) && memchr(bytes, 0, sizeof(bytes)) == NULL) {
        fprintf(stderr, "...");
    }
}

static void print_item(const VM *vm, const LogItem *item) {
    uint32_t value = item->reg >= 0 ? vm->registers[item->reg] : 0;
    uint32_t size = item->memory ? item->size : 4;

    fprintf(stderr, " %s=", item->text);
    if (item->memory) {
        uint8_t bytes[4] = { 0 };
        if (item->format == 's') {
            print_string(vm, value + item->offset);
            return;
        }
        if (memory_peek(vm, value + item->offset, bytes, size) < size) {
            fprintf(stderr, "?");
            return;
        }
        value = read_le32(bytes);
    }
    switch (item->format) {
        case 'd':
            fprintf(stderr, "%d", size == 1 ? (int8_t)value : size == 2 ? (int16_t)value : (int32_t)value);
            break;
        case 'u':
            fprintf(stderr, "%u", value);
            break;
        case 'c':
            fprintf(stderr, value >= 32 && value < 127 ? "'%c'" : "'\\x%02X'", value);
            break;
        case 's':
            print_string(vm, value);
            break;
        case 'f': {
            float number;
            memcpy(&number, &value, sizeof(number));
            fprintf(stderr, "%g", (double)number);
            break;
        }
        default:
            fprintf(stderr, "0x%0*X", (int)size * 2, value);
            break;
    }
}

static void print_logpoint(const VM *vm, const Logpoint *logpoint) {
    char where[80];
    debug_describe(vm->debug_info, logpoint->address, where, sizeof(where));
    fflush(stdout);
    fprintf(stderr, "log 0x%04X%s%s", logpoint->address, where[0] ? " " : "", where);
    for (int i = 0; i < logpoint->item_count; i++) {
        print_item(vm, &logpoint->items[i]);
    }
    fprintf(stderr, "\n");
}

// A term is a register, a number or a symbol; returns 1 for a register, 0 for a value, -1 if unknown
static int parse_term(const VM *vm, const char *text, size_t length, int *reg, uint32_t *value) {
    char term[64];
    if (length == 0 || length >= sizeof(term)) {
        return -1;
    }
    memcpy(term, text, length);
    term[length] = '\0';

    if ((*reg = isa_register_index(term)) >= 0) {
        return 1;
    }
    char *end;
    unsigned long number = strtoul(term, &end, 0);
    if (isdigit((unsigned char)term[0]) && *end == '\0' && number <= 0xFFFFFFFFul) {
        *value = (uint32_t)number;
        return 0;
    }
    const Symbol *sym = debug_symbol_named(vm->debug_info, term);
    if (sym) {
        *value = sym->address;
        return 0;
    }
    return -1;
}

// An item is a register or [ADDRESS], where the address adds and subtracts one register, numbers
// and symbols, followed by :FORMAT with an optional size b or w and one of x, d, u, c, s and f
static int parse_item(const VM *vm, const char *text, size_t length, LogItem *item, char *error, size_t size) {
    const char *colon = memchr(text, ':', length);
    size_t body = colon ? (size_t)(colon - text) : length;

    memset(item, 0, sizeof(*item));
    item->reg = -1;
    item->size = 4;
    item->format = 'x';
    snprintf(item->text, sizeof(item->text), "%.*s", (int)body, text);

    for (const char *f = colon ? colon + 1 : text + length; f < text + length; f++) {
        if (*f == 'b' || *f == 'w') {
            item->size = *f == 'b' ? 1 : 2;
        } else if (strchr("xducsf", *f)) {
            item->format = *f;
        } else {
            snprintf(error, size, "unknown format '%c' in %.*s", *f, (int)length, text);
            return 1;
        }
    }

    if (body < 2 || text[0] != '[' || text[body - 1] != ']') {
        if (parse_term(vm, text, body, &item->reg, &item->offset) != 1) {
            snprintf(error, size, "%s is not a register or a [memory] operand", item->text);
            return 1;
        }
        return 0;
    }

    item->memory = 1;
    const char *p = text + 1, *end = text + body - 1;
    int sign = 1;
    while (p < end) {
        const char *next = p;
        while (next < end && *next != '+' && *next != '-') {
            next++;
        }
        int reg;
        uint32_t value = 0;
        int kind = parse_term(vm, p, (size_t)(next - p), &reg, &value);
        if (kind < 0 || (kind == 1 && (sign < 0 || item->reg >= 0))) {
            snprintf(error, size, "cannot read the address in %s", item->text);
            return 1;
        }
        if (kind == 1) {
            item->reg = reg;
        } else {
            item->offset += sign > 0 ? value : 0u - value;
        }
        sign = next < end && *next == '-' ? -1 : 1;
        p = next < end ? next + 1 : end;
    }
    return 0;
}

int monitor_add_logpoint(Monitor *monitor, const VM *vm, const char *spec, char *error, size_t size) {
    const char *colon = strchr(spec, ':');
    size_t length = colon ? (size_t)(colon - spec) : strlen(spec);
    Logpoint logpoint = { 0 };
    int reg;

    if (parse_term(vm, spec, length, &reg, &logpoint.address) != 0) {
        snprintf(error, size, "unknown location %.*s", (int)length, spec);
        return 1;
    }
    for (const char *item = colon ? colon + 1 : NULL; item && *item;) {
        const char *comma = strchr(item, ',');
        size_t item_length = comma ? (size_t)(comma - item) : strlen(item);
        if (logpoint.item_count == LOG_ITEMS) {
            snprintf(error, size, "more than %d items", LOG_ITEMS);
            return 1;
        }
        if (parse_item(vm, item, item_length, &logpoint.items[logpoint.item_count++], error, size)) {
            return 1;
        }
        item = comma ? comma + 1 : NULL;
    }

    Logpoint *grown = realloc(monitor->logpoints, (size_t)(monitor->logpoint_count + 1) * sizeof(Logpoint));
    if (!grown) {
        snprintf(error, size, "out of memory");
        return 1;
    }
    monitor->logpoints = grown;
    monitor->logpoints[monitor->logpoint_count++] = logpoint;
    return 0;
}

void monitor_free(Monitor *monitor) {
    free(monitor->counts);
    free(monitor->history);
    free(monitor->logpoints);
    memset(monitor, 0, sizeof(*monitor));
}

// Runs like vm_run, optionally printing each instruction on stderr before executing it, counting
// how often each instruction of the loaded code executes, remembering the last instructions and
// printing the values that logpoints ask for
int monitor_run(VM *vm, Monitor *monitor) {
    uint32_t before[16];

    if (!monitor->trace && !monitor->counts && !monitor->history && !monitor->logpoint_count) {
        return vm_run(vm);
    }
    while (!vm->halted) {
        for (int i = 0; i < monitor->logpoint_count; i++) {
            if (monitor->logpoints[i].address == vm->registers[R3_PC]) {
                print_logpoint(vm, &monitor->logpoints[i]);
            }
        }
        if (monitor->trace) {
            fflush(stdout);
            print_instruction(vm, vm->registers[R3_PC], 0);
            fprintf(stderr, "\n");
        }
        if (monitor->history) {
            memcpy(before, vm->registers, sizeof(before));
        }

        int result = vm_step(vm);
        if (result != VM_ERROR_NONE) {
            return result;
        }
        if (monitor->trace && vm->exception) {
            fprintf(stderr, "vm: exception %u: %s\n", vm->exception, vm->exception_message);
        }
        if (monitor->counts && vm->error_pc < vm->code_end) {
            monitor->counts[vm->error_pc / 4]++;
        }
        if (monitor->history) {
            record_history(vm, monitor, before);
        }
    }
    return VM_ERROR_NONE;
}

// Prints the remembered instructions, oldest first, each with the registers it changed
void monitor_report_history(const VM *vm, const Monitor *monitor) {
    uint64_t shown = monitor->history_count < monitor->history_size ? monitor->history_count : monitor->history_size;
    if (!monitor->history || shown == 0) {
        return;
    }
    fprintf(stderr, "vm: the last %llu instructions:\n", (unsigned long long)shown);
    for (uint64_t n = monitor->history_count - shown; n < monitor->history_count; n++) {
        const HistoryEntry *entry = &monitor->history[n % monitor->history_size];
        print_instruction(vm, entry->pc, entry->changed || entry->exception ? 24 : 0);
        for (int i = 0; i < 16; i++) {
            if (entry->changed & (1u << i)) {
                fprintf(stderr, " %s=0x%08X", isa_register_name((uint8_t)i), entry->registers[i]);
            }
        }
        if (entry->exception) {
            fprintf(stderr, " exception %u", entry->exception);
        }
        fprintf(stderr, "\n");
    }
}

typedef struct {
    uint32_t address;
    uint64_t count;
} ProfileEntry;

static int compare_entries(const void *a, const void *b) {
    const ProfileEntry *x = a, *y = b;
    if (x->count != y->count) {
        return x->count < y->count ? 1 : -1;
    }
    return x->address < y->address ? -1 : x->address > y->address;
}

// Sums the counts under the closest preceding label, or per address without debug information
void monitor_report_profile(const VM *vm, const Monitor *monitor) {
    enum { SHOWN = 15 };
    uint32_t slots = (vm->code_end + 3) / 4;
    ProfileEntry *entries = calloc(slots ? slots : 1, sizeof(ProfileEntry));
    size_t used = 0;
    uint64_t total = 0;

    if (!entries) {
        return;
    }
    const uint32_t *counts = monitor->counts;
    for (uint32_t i = 0; i < slots; i++) {
        if (counts[i] == 0) {
            continue;
        }
        const Symbol *sym = debug_symbol_near(vm->debug_info, i * 4);
        uint32_t key = sym ? sym->address : i * 4;
        size_t e = 0;
        while (e < used && entries[e].address != key) {
            e++;
        }
        if (e == used) {
            entries[used++].address = key;
        }
        entries[e].count += counts[i];
        total += counts[i];
    }
    qsort(entries, used, sizeof(ProfileEntry), compare_entries);

    fprintf(stderr, "vm: profile of %llu instructions\n", (unsigned long long)total);
    fprintf(stderr, "%12s  %6s  %s\n", "count", "share", "location");
    for (size_t e = 0; e < used && e < SHOWN; e++) {
        char where[80];
        debug_describe(vm->debug_info, entries[e].address, where, sizeof(where));
        fprintf(stderr, "%12llu  %5.1f%%  0x%04X%s%s\n", (unsigned long long)entries[e].count,
                100.0 * (double)entries[e].count / (double)total, entries[e].address, where[0] ? " " : "", where);
    }
    if (used > SHOWN) {
        fprintf(stderr, "%12s  %6s  %zu more\n", "", "", used - SHOWN);
    }
    free(entries);
}

typedef struct {
    int file;               // index of the source file in the order files first appear
    uint32_t line;
    uint64_t count;
    const SourceLine *source;
} CoverageLine;

static int compare_lines(const void *a, const void *b) {
    const CoverageLine *x = a, *y = b;
    if (x->file != y->file) {
        return x->file < y->file ? -1 : 1;
    }
    return x->line < y->line ? -1 : x->line > y->line;
}

// Lists every source line that produced code with the number of times it ran, marking lines that
// never ran with #####, under a summary line for each file
int monitor_write_coverage(const VM *vm, const Monitor *monitor, const char *path) {
    const DebugInfo *info = vm->debug_info;
    uint32_t total = info ? info->source_line_count : 0, used = 0;
    CoverageLine *lines = calloc(total ? total : 1, sizeof(CoverageLine));
    const char **files = calloc(total ? total : 1, sizeof(char *));
    int file_count = 0;

    if (!lines || !files) {
        free(lines);
        free(files);
        return -1;
    }
    for (uint32_t i = 0; i < total; i++) {
        const SourceLine *source = &info->source_lines[i];
        const char *name = source->source_file ? source->source_file : "?";
        if (source->address >= vm->code_end) {
            continue;
        }
        int file = 0;
        while (file < file_count && strcmp(files[file], name) != 0) {
            file++;
        }
        if (file == file_count) {
            files[file_count++] = name;
        }
        lines[used++] = (CoverageLine){ file, source->line_num, monitor->counts[source->address / 4], source };
    }
    qsort(lines, used, sizeof(CoverageLine), compare_lines);

    // Lines that several macro expansions share add up their counts
    uint32_t merged = 0;
    for (uint32_t i = 0; i < used; i++) {
        if (merged > 0 && lines[merged - 1].file == lines[i].file && lines[merged - 1].line == lines[i].line) {
            lines[merged - 1].count += lines[i].count;
        } else {
            lines[merged++] = lines[i];
        }
    }

    FILE *out = strcmp(path, "-") == 0 ? stdout : fopen(path, "w");
    if (!out) {
        free(lines);
        free(files);
        return -1;
    }
    uint32_t all_ran = 0;
    for (uint32_t first = 0, end; first < merged; first = end) {
        uint32_t ran = 0;
        for (end = first; end < merged && lines[end].file == lines[first].file; end++) {
            ran += lines[end].count != 0;
        }
        all_ran += ran;
        fprintf(out, "%s: %u of %u lines ran\n", files[lines[first].file], ran, end - first);
        for (uint32_t i = first; i < end; i++) {
            char count[24];
            snprintf(count, sizeof(count), "%llu", (unsigned long long)lines[i].count);
            fprintf(out, "%9s:%5u: %s\n", lines[i].count ? count : "#####", lines[i].line, lines[i].source->source);
        }
    }
    if (file_count > 1) {
        fprintf(out, "total: %u of %u lines ran\n", all_ran, merged);
    }

    int result = ferror(out) ? -1 : 0;
    if (out != stdout && fclose(out) != 0) {
        result = -1;
    }
    free(lines);
    free(files);
    return result;
}
