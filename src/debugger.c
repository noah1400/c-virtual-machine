#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"
#include "debug.h"
#include "debugger.h"
#include "disassembler.h"
#include "vm.h"

#define MAX_BREAKPOINTS 32
#define MAX_WORDS 4

typedef struct {
    uint32_t address;
    char location[64];
} Breakpoint;

typedef struct {
    VM *vm;
    Breakpoint breakpoints[MAX_BREAKPOINTS];
    int breakpoint_count;
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

static void show_location(const Debugger *dbg) {
    VM *vm = dbg->vm;
    uint32_t pc = vm->registers[R3_PC];

    printf("0x%04X", pc);
    print_symbolic(dbg, pc);

    const SourceLine *line = debug_line_at(vm->debug_info, pc);
    if (line) {
        printf("  %s:%u: %s", line->source_file ? line->source_file : "?", line->line_num,
               line->source ? line->source : "");
    }
    printf("\n");

    Instruction instr;
    if (vm_peek_instruction(vm, pc, &instr)) {
        char text[160];
        disasm_format(&instr, vm->debug_info, text, sizeof(text));
        printf("Next instruction: %s\n", text);
    }
}

static int breakpoint_index(const Debugger *dbg, uint32_t address) {
    for (int i = 0; i < dbg->breakpoint_count; i++) {
        if (dbg->breakpoints[i].address == address) {
            return i;
        }
    }
    return -1;
}

// Executes one instruction; returns 0 and reports why when execution cannot continue
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
    if (vm->halted) {
        printf("Program halted\n");
        return 0;
    }
    return 1;
}

static int at_breakpoint(Debugger *dbg) {
    int index = breakpoint_index(dbg, dbg->vm->registers[R3_PC]);
    if (index >= 0) {
        printf("Breakpoint %d at 0x%04X\n", index + 1, dbg->breakpoints[index].address);
        return 1;
    }
    return 0;
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

    while (step_instruction(dbg) && !at_breakpoint(dbg)) {
        const SourceLine *line = debug_line_at(vm->debug_info, vm->registers[R3_PC]);
        if (!start || line != start) {
            break;
        }
    }
    cpu_dump_registers(vm);
}

static void cmd_continue(Debugger *dbg) {
    while (step_instruction(dbg) && !at_breakpoint(dbg)) {
    }
    cpu_dump_registers(dbg->vm);
}

static void cmd_break(Debugger *dbg, int argc, char **argv) {
    uint32_t address;
    if (argc < 2) {
        printf("Usage: break <address|symbol>\n");
        return;
    }
    if (!parse_location(dbg, argv[1], &address)) {
        return;
    }
    if (breakpoint_index(dbg, address) >= 0) {
        printf("Breakpoint already set at 0x%04X\n", address);
        return;
    }
    if (dbg->breakpoint_count >= MAX_BREAKPOINTS) {
        printf("Maximum number of breakpoints reached\n");
        return;
    }

    Breakpoint *bp = &dbg->breakpoints[dbg->breakpoint_count++];
    bp->address = address;
    snprintf(bp->location, sizeof(bp->location), "%s", argv[1]);

    printf("Breakpoint %d at 0x%04X", dbg->breakpoint_count, address);
    print_symbolic(dbg, address);
    printf("\n");
}

static void cmd_breakpoints(const Debugger *dbg) {
    if (dbg->breakpoint_count == 0) {
        printf("No breakpoints set\n");
        return;
    }
    for (int i = 0; i < dbg->breakpoint_count; i++) {
        const Breakpoint *bp = &dbg->breakpoints[i];
        printf("%2d  0x%04X", i + 1, bp->address);
        print_symbolic(dbg, bp->address);
        const SourceLine *line = debug_line_at(dbg->vm->debug_info, bp->address);
        if (line) {
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

static void cmd_help(void) {
    printf("Debugger commands (numbers are decimal or 0x-prefixed hex):\n");
    printf("  s, step [N]              Execute N instructions (default 1)\n");
    printf("  n, next                  Run until the source line changes\n");
    printf("  c, continue              Run until a breakpoint, halt or fault\n");
    printf("  b, break ADDR|SYMBOL     Set a breakpoint\n");
    printf("  lb, breakpoints          List breakpoints\n");
    printf("  ls, symbols              List symbols\n");
    printf("  m, memory ADDR [N]       Dump N bytes of memory (default 16)\n");
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
        } else if (is_command(cmd, "c", "continue")) {
            cmd_continue(&dbg);
            show_location(&dbg);
        } else if (is_command(cmd, "b", "break")) {
            cmd_break(&dbg, argc, argv);
        } else if (is_command(cmd, "lb", "breakpoints")) {
            cmd_breakpoints(&dbg);
        } else if (is_command(cmd, "ls", "symbols")) {
            cmd_symbols(&dbg);
        } else if (is_command(cmd, "m", "memory")) {
            cmd_memory(&dbg, argc, argv);
        } else if (is_command(cmd, "r", "registers")) {
            cpu_dump_registers(vm);
        } else if (is_command(cmd, "h", "help")) {
            cmd_help();
        } else {
            printf("Unknown command '%s'. Type 'h' for help.\n", cmd);
        }
    }

    return vm->last_error;
}
