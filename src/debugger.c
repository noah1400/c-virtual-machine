#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "binfmt.h"
#include "cpu.h"
#include "debug.h"
#include "debugger.h"
#include "disassembler.h"
#include "vm.h"

#define MAX_BREAKPOINTS 32
#define MAX_WORDS 4
#define CONTEXT_LINES 2

// Watchpoints stop after the word at their address changes, breakpoints before executing it
typedef struct {
    uint32_t address;
    int watch;
    uint32_t value;
} Breakpoint;

// The most recently shown source file, split into lines
typedef struct {
    char *path;
    char *text;
    char **lines;
    uint32_t count;
} SourceCache;

typedef struct {
    VM *vm;
    Breakpoint breakpoints[MAX_BREAKPOINTS];
    int breakpoint_count;
    SourceCache source;
} Debugger;

static int is_command(const char *word, const char *short_name, const char *long_name) {
    return strcmp(word, short_name) == 0 || strcmp(word, long_name) == 0;
}

// Numbers are decimal unless prefixed with 0x
static int parse_number(const char *text, uint32_t *value) {
    int base = 10;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    }
    if (*text == '\0') {
        return 0;
    }
    char *end;
    unsigned long parsed = strtoul(text, &end, base);
    if (*end != '\0' || parsed > 0xFFFFFFFFul) {
        return 0;
    }
    *value = (uint32_t)parsed;
    return 1;
}

static int parse_location(const Debugger *dbg, const char *text, uint32_t *address) {
    if (parse_number(text, address)) {
        return 1;
    }
    const Symbol *sym = debug_symbol_named(dbg->vm->debug_info, text);
    if (sym) {
        *address = sym->address;
        return 1;
    }
    printf("Unknown location: %s\n", text);
    return 0;
}

static void print_symbolic(const Debugger *dbg, uint32_t address) {
    const Symbol *sym = debug_symbol_near(dbg->vm->debug_info, address);
    if (sym && address == sym->address) {
        printf(" <%s>", sym->name);
    } else if (sym) {
        printf(" <%s+%u>", sym->name, address - sym->address);
    }
}

static void source_free(SourceCache *cache) {
    free(cache->path);
    free(cache->text);
    free(cache->lines);
    memset(cache, 0, sizeof(*cache));
}

// Returns the text of a 1-based line of a source file, or NULL when the file cannot be read
static const char *source_line(SourceCache *cache, const char *path, uint32_t number) {
    if (!cache->path || strcmp(cache->path, path) != 0) {
        uint32_t size;
        const char *problem;
        uint8_t *bytes = read_binary_file(path, &size, &problem);

        source_free(cache);
        cache->path = malloc(strlen(path) + 1);
        if (!cache->path) {
            free(bytes);
            return NULL;
        }
        strcpy(cache->path, path);
        if (!bytes) {
            return NULL;
        }

        cache->text = (char *)bytes;

        uint32_t lines = 1;
        for (uint32_t i = 0; i < size; i++) {
            lines += cache->text[i] == '\n';
        }
        cache->lines = malloc(lines * sizeof(char *));
        if (!cache->lines) {
            return NULL;
        }
        char *line = cache->text;
        for (cache->count = 0; line; cache->count++) {
            cache->lines[cache->count] = line;
            line = strchr(line, '\n');
            if (line) {
                *line++ = '\0';
            }
            size_t length = strlen(cache->lines[cache->count]);
            if (length > 0 && cache->lines[cache->count][length - 1] == '\r') {
                cache->lines[cache->count][length - 1] = '\0';
            }
        }
    }

    return cache->lines && number >= 1 && number <= cache->count ? cache->lines[number - 1] : NULL;
}

static void show_source(Debugger *dbg, const SourceLine *line) {
    if (!line->source_file || !source_line(&dbg->source, line->source_file, line->line_num)) {
        printf("  %s:%u: %s\n", line->source_file ? line->source_file : "?", line->line_num,
               line->source ? line->source : "");
        return;
    }

    printf("  %s:\n", line->source_file);
    uint32_t first = line->line_num > CONTEXT_LINES ? line->line_num - CONTEXT_LINES : 1;
    for (uint32_t n = first; n <= line->line_num + CONTEXT_LINES; n++) {
        const char *text = source_line(&dbg->source, line->source_file, n);
        if (text) {
            printf("%s%5u  %s\n", n == line->line_num ? "=> " : "   ", n, text);
        }
    }
}

// Shows where execution stands: the next instruction, or the one that faulted
static void show_location(Debugger *dbg) {
    VM *vm = dbg->vm;
    int faulted = vm->last_error != VM_ERROR_NONE;
    uint32_t pc = faulted ? vm->error_pc : vm->registers[R3_PC];

    if (vm->halted) {
        return;
    }

    printf("%s0x%04X", faulted ? "Faulted at " : "", pc);
    print_symbolic(dbg, pc);
    printf("\n");

    const SourceLine *line = debug_line_at(vm->debug_info, pc);
    if (line && line->address == pc) {
        show_source(dbg, line);
    }

    Instruction instr;
    if (pc % 4 == 0 && vm_peek_instruction(vm, pc, &instr)) {
        char text[160];
        disasm_format(&instr, pc, vm->debug_info, text, sizeof(text));
        printf("%s: %s\n", faulted ? "Instruction" : "Next instruction", text);
    }
}

static int breakpoint_index(const Debugger *dbg, uint32_t address, int watch) {
    for (int i = 0; i < dbg->breakpoint_count; i++) {
        if (dbg->breakpoints[i].address == address && dbg->breakpoints[i].watch == watch) {
            return i;
        }
    }
    return -1;
}

// Reports every watched word that changed since it was last seen
static int watch_triggered(Debugger *dbg) {
    int triggered = 0;
    for (int i = 0; i < dbg->breakpoint_count; i++) {
        Breakpoint *bp = &dbg->breakpoints[i];
        uint32_t value = bp->watch ? read_le32(dbg->vm->memory + bp->address) : 0;
        if (bp->watch && value != bp->value) {
            printf("Watchpoint %d: 0x%04X changed from 0x%08X to 0x%08X\n", i + 1, bp->address, bp->value, value);
            bp->value = value;
            triggered = 1;
        }
    }
    return triggered;
}

// Executes one instruction; returns 0 and reports why when execution has to stop
static int step_instruction(Debugger *dbg) {
    VM *vm = dbg->vm;

    if (vm->halted) {
        printf("Program has halted\n");
        return 0;
    }
    if (vm_step(vm) != VM_ERROR_NONE) {
        printf("Error: %s\n", vm_get_error_message(vm));
        return 0;
    }
    if (vm->exception) {
        printf("Exception %u: %s\n", vm->exception, vm->exception_message);
        return 0;
    }
    if (watch_triggered(dbg)) {
        return 0;
    }
    if (vm->halted) {
        printf("Program halted\n");
        return 0;
    }
    if (vm->break_requested) {
        vm->break_requested = 0;
        printf("DEBUG instruction at 0x%04X\n", vm->error_pc);
        return 0;
    }
    return 1;
}

static int at_breakpoint(Debugger *dbg) {
    int index = breakpoint_index(dbg, dbg->vm->registers[R3_PC], 0);
    if (index >= 0) {
        printf("Breakpoint %d at 0x%04X\n", index + 1, dbg->breakpoints[index].address);
        return 1;
    }
    return 0;
}

static uint8_t next_opcode(const Debugger *dbg) {
    Instruction instr;
    return vm_peek_instruction(dbg->vm, dbg->vm->registers[R3_PC], &instr) ? instr.opcode : NOP_OP;
}

// Steps one instruction, running a CALL until it returns; RET #n leaves SP above its old value
static int step_over(Debugger *dbg) {
    VM *vm = dbg->vm;
    Instruction call;
    uint32_t size = vm_peek_instruction(vm, vm->registers[R3_PC], &call);

    if (!size || call.opcode != CALL_OP) {
        return step_instruction(dbg);
    }

    uint32_t return_address = vm->registers[R3_PC] + size;
    uint32_t sp = vm->registers[R2_SP];
    if (!step_instruction(dbg)) {
        return 0;
    }
    while (vm->registers[R3_PC] != return_address || vm->registers[R2_SP] < sp) {
        if (!step_instruction(dbg) || at_breakpoint(dbg)) {
            return 0;
        }
    }
    return 1;
}

static void cmd_step(Debugger *dbg, int argc, char **argv) {
    uint32_t count = 1;
    if (argc > 1 && (!parse_number(argv[1], &count) || count == 0)) {
        printf("Usage: step [count]\n");
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (!step_instruction(dbg) || (i + 1 < count && at_breakpoint(dbg))) {
            break;
        }
    }
    cpu_dump_registers(dbg->vm);
}

static void cmd_next(Debugger *dbg) {
    VM *vm = dbg->vm;
    const SourceLine *start = debug_line_at(vm->debug_info, vm->registers[R3_PC]);

    while (step_over(dbg) && !at_breakpoint(dbg)) {
        const SourceLine *line = debug_line_at(vm->debug_info, vm->registers[R3_PC]);
        if (!start || line != start) {
            break;
        }
    }
    cpu_dump_registers(vm);
}

// Runs until the current subroutine returns to its caller
static void cmd_finish(Debugger *dbg) {
    int depth = 0;

    for (;;) {
        uint8_t opcode = next_opcode(dbg);
        if (!step_instruction(dbg)) {
            break;
        }
        if (opcode == CALL_OP) {
            depth++;
        } else if (opcode == RET_OP && depth-- == 0) {
            break;
        }
        if (at_breakpoint(dbg)) {
            break;
        }
    }
    cpu_dump_registers(dbg->vm);
}

static void cmd_continue(Debugger *dbg) {
    while (step_instruction(dbg) && !at_breakpoint(dbg)) {
    }
    cpu_dump_registers(dbg->vm);
}

static void add_breakpoint(Debugger *dbg, uint32_t address, int watch) {
    const char *kind = watch ? "Watchpoint" : "Breakpoint";

    if (breakpoint_index(dbg, address, watch) >= 0) {
        printf("%s already set at 0x%04X\n", kind, address);
        return;
    }
    if (dbg->breakpoint_count >= MAX_BREAKPOINTS) {
        printf("Maximum number of breakpoints reached\n");
        return;
    }

    Breakpoint *bp = &dbg->breakpoints[dbg->breakpoint_count++];
    bp->address = address;
    bp->watch = watch;
    bp->value = watch ? read_le32(dbg->vm->memory + address) : 0;
    printf("%s %d at 0x%04X", kind, dbg->breakpoint_count, address);
    print_symbolic(dbg, address);
    printf("\n");
}

static void cmd_break(Debugger *dbg, int argc, char **argv) {
    uint32_t address;
    if (argc < 2) {
        printf("Usage: break <address|symbol>\n");
        return;
    }
    if (parse_location(dbg, argv[1], &address)) {
        add_breakpoint(dbg, address, 0);
    }
}

static void cmd_watch(Debugger *dbg, int argc, char **argv) {
    uint32_t address;
    if (argc < 2) {
        printf("Usage: watch <address|symbol>\n");
        return;
    }
    if (!parse_location(dbg, argv[1], &address)) {
        return;
    }
    if (address > dbg->vm->memory_size - 4) {
        printf("Address out of range\n");
        return;
    }
    add_breakpoint(dbg, address, 1);
}

static void cmd_delete(Debugger *dbg, int argc, char **argv) {
    uint32_t number;
    if (argc < 2 || !parse_number(argv[1], &number) || number == 0 || number > (uint32_t)dbg->breakpoint_count) {
        printf("Usage: delete <number>\n");
        return;
    }
    const char *kind = dbg->breakpoints[number - 1].watch ? "watchpoint" : "breakpoint";
    memmove(&dbg->breakpoints[number - 1], &dbg->breakpoints[number],
            (size_t)(dbg->breakpoint_count - (int)number) * sizeof(Breakpoint));
    dbg->breakpoint_count--;
    printf("Deleted %s %u\n", kind, number);
}

static void cmd_breakpoints(const Debugger *dbg) {
    if (dbg->breakpoint_count == 0) {
        printf("No breakpoints or watchpoints set\n");
        return;
    }
    for (int i = 0; i < dbg->breakpoint_count; i++) {
        const Breakpoint *bp = &dbg->breakpoints[i];
        printf("%2d  0x%04X", i + 1, bp->address);
        print_symbolic(dbg, bp->address);
        const SourceLine *line = debug_line_at(dbg->vm->debug_info, bp->address);
        if (bp->watch) {
            printf("  watch, value 0x%08X", bp->value);
        } else if (line) {
            printf("  %s:%u", line->source_file ? line->source_file : "?", line->line_num);
        }
        printf("\n");
    }
}

static void cmd_symbols(const Debugger *dbg) {
    const DebugInfo *info = dbg->vm->debug_info;
    if (!info || info->symbol_count == 0) {
        printf("No debug information available\n");
        return;
    }
    static const char *const types[] = { "CODE", "DATA", "CONST" };
    for (uint32_t i = 0; i < info->symbol_count; i++) {
        const Symbol *sym = &info->symbols[i];
        printf("0x%04X  %-5s  %-24s %s:%u\n", sym->address, sym->type <= SYMBOL_CONST ? types[sym->type] : "?",
               sym->name, sym->source_file ? sym->source_file : "?", sym->line_num);
    }
}

static void cmd_memory(const Debugger *dbg, int argc, char **argv) {
    VM *vm = dbg->vm;
    uint32_t address, count = 16;

    if (argc < 2) {
        printf("Usage: memory <address|symbol> [count]\n");
        return;
    }
    if (!parse_location(dbg, argv[1], &address) || (argc > 2 && !parse_number(argv[2], &count))) {
        return;
    }
    if (address >= vm->memory_size) {
        printf("Address out of range\n");
        return;
    }
    if (count > vm->memory_size - address) {
        count = vm->memory_size - address;
    }
    disasm_hexdump(vm->memory + address, address, count);
}

static void cmd_disassemble(const Debugger *dbg, int argc, char **argv) {
    VM *vm = dbg->vm;
    uint32_t address = vm->registers[R3_PC], count = 8;

    if ((argc > 1 && !parse_location(dbg, argv[1], &address)) || (argc > 2 && !parse_number(argv[2], &count))) {
        return;
    }
    for (uint32_t i = 0, size; i < count; i++, address += size) {
        Instruction instr;
        char text[160];
        if (!(size = vm_peek_instruction(vm, address, &instr))) {
            break;
        }
        const Symbol *sym = debug_symbol_at(vm->debug_info, address);
        if (sym) {
            printf("%s:\n", sym->name);
        }
        disasm_format(&instr, address, vm->debug_info, text, sizeof(text));
        printf("%s 0x%04X  %s\n", address == vm->registers[R3_PC] ? "=>" : "  ", address, text);
    }
}

static void cmd_stack(const Debugger *dbg, int argc, char **argv) {
    VM *vm = dbg->vm;
    uint32_t count = 8;
    uint32_t sp = vm->registers[R2_SP];
    uint32_t top = vm->control[CR_SHI];

    if (argc > 1 && !parse_number(argv[1], &count)) {
        printf("Usage: stack [count]\n");
        return;
    }
    if (sp < vm->control[CR_SLO] || sp > top || top > vm->memory_size) {
        printf("SP 0x%08X is outside the stack\n", sp);
        return;
    }
    if (sp == top) {
        printf("The stack is empty\n");
    }
    for (uint32_t address = sp; address + 4 <= top && count > 0; address += 4, count--) {
        uint32_t value = read_le32(vm->memory + address);
        printf("0x%04X  0x%08X", address, value);
        if (address == sp) {
            printf("  <- SP");
        }
        if (address == vm->registers[R1_BP]) {
            printf("  <- BP");
        }
        const Symbol *sym = debug_symbol_near(vm->debug_info, value);
        if (sym && sym->type == SYMBOL_CODE && value < vm->code_end) {
            printf("  ");
            print_symbolic(dbg, value);
        }
        printf("\n");
    }
}

static void cmd_help(void) {
    printf("Debugger commands (numbers are decimal or 0x-prefixed hex):\n");
    printf("  s, step [N]              Execute N instructions (default 1)\n");
    printf("  n, next                  Run to the next source line, stepping over calls\n");
    printf("  f, finish                Run until the current subroutine returns\n");
    printf("  c, continue              Run until a breakpoint, DEBUG instruction, halt or fault\n");
    printf("  b, break ADDR|SYMBOL     Set a breakpoint\n");
    printf("  w, watch ADDR|SYMBOL     Stop when the word at ADDR changes\n");
    printf("  d, delete N              Delete breakpoint or watchpoint N\n");
    printf("  lb, breakpoints          List breakpoints and watchpoints\n");
    printf("  ls, symbols              List symbols\n");
    printf("  x, disas [ADDR] [N]      Disassemble N instructions (default: 8 at PC)\n");
    printf("  m, memory ADDR [N]       Dump N bytes of memory (default 16)\n");
    printf("  stack [N]                Show N words from the top of the stack (default 8)\n");
    printf("  r, registers             Show registers and flags\n");
    printf("  h, help                  Show this help\n");
    printf("  q, quit                  Leave the debugger\n");
}

int debugger_run(VM *vm) {
    Debugger dbg = { .vm = vm };
    char line[256];

    printf("Debugger ready. Type 'h' for help.\n");
    show_location(&dbg);

    for (;;) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }

        char *argv[MAX_WORDS];
        int argc = 0;
        for (char *word = strtok(line, " \t\r\n"); word && argc < MAX_WORDS; word = strtok(NULL, " \t\r\n")) {
            argv[argc++] = word;
        }
        if (argc == 0) {
            continue;
        }

        const char *cmd = argv[0];
        if (is_command(cmd, "q", "quit")) {
            break;
        } else if (is_command(cmd, "s", "step")) {
            cmd_step(&dbg, argc, argv);
            show_location(&dbg);
        } else if (is_command(cmd, "n", "next")) {
            cmd_next(&dbg);
            show_location(&dbg);
        } else if (is_command(cmd, "f", "finish")) {
            cmd_finish(&dbg);
            show_location(&dbg);
        } else if (is_command(cmd, "c", "continue")) {
            cmd_continue(&dbg);
            show_location(&dbg);
        } else if (is_command(cmd, "b", "break")) {
            cmd_break(&dbg, argc, argv);
        } else if (is_command(cmd, "w", "watch")) {
            cmd_watch(&dbg, argc, argv);
        } else if (is_command(cmd, "d", "delete")) {
            cmd_delete(&dbg, argc, argv);
        } else if (is_command(cmd, "lb", "breakpoints")) {
            cmd_breakpoints(&dbg);
        } else if (is_command(cmd, "ls", "symbols")) {
            cmd_symbols(&dbg);
        } else if (is_command(cmd, "x", "disas")) {
            cmd_disassemble(&dbg, argc, argv);
        } else if (is_command(cmd, "m", "memory")) {
            cmd_memory(&dbg, argc, argv);
        } else if (is_command(cmd, "stack", "stack")) {
            cmd_stack(&dbg, argc, argv);
        } else if (is_command(cmd, "r", "registers")) {
            cpu_dump_registers(vm);
        } else if (is_command(cmd, "h", "help")) {
            cmd_help();
        } else {
            printf("Unknown command '%s'. Type 'h' for help.\n", cmd);
        }
    }

    source_free(&dbg.source);
    return vm->last_error;
}
