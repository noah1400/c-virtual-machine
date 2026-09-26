#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "debugger.h"
#include "disassembler.h"
#include "vm.h"

#define DEFAULT_MEMORY_KB 64
#define MAX_MEMORY_KB     65536

typedef struct {
    const char *program;
    int arg_count;
    char **args;
    uint32_t memory_size;
    int debug;
    int disassemble;
    int trace;
    int verbose;
} Options;

static void print_usage(FILE *out, const char *name) {
    fprintf(out, "Usage: %s [options] program.bin [arguments...]\n", name);
    fprintf(out, "Options:\n");
    fprintf(out, "  -d        Start the interactive debugger\n");
    fprintf(out, "  -D        Disassemble the program instead of running it\n");
    fprintf(out, "  -m KB     Memory size in KB, at least %d (default %d)\n", DEFAULT_MEMORY_KB, DEFAULT_MEMORY_KB);
    fprintf(out, "  -t        Trace every executed instruction on stderr\n");
    fprintf(out, "  -v        Report loading and execution statistics on stderr\n");
    fprintf(out, "  -h        Show this help\n");
}

// Returns 1 to run, 0 to exit successfully, -1 on a usage error
static int parse_options(int argc, char **argv, Options *opts) {
    memset(opts, 0, sizeof(*opts));
    opts->memory_size = DEFAULT_MEMORY_KB * 1024;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (arg[0] != '-' || arg[1] == '\0') {
            // Everything from the program path on belongs to the program
            opts->program = arg;
            opts->arg_count = argc - i;
            opts->args = argv + i;
            break;
        } else if (strcmp(arg, "-d") == 0) {
            opts->debug = 1;
        } else if (strcmp(arg, "-D") == 0) {
            opts->disassemble = 1;
        } else if (strcmp(arg, "-t") == 0) {
            opts->trace = 1;
        } else if (strcmp(arg, "-v") == 0) {
            opts->verbose = 1;
        } else if (strcmp(arg, "-h") == 0) {
            print_usage(stdout, argv[0]);
            return 0;
        } else if (strcmp(arg, "-m") == 0) {
            char *end = NULL;
            long kb = i + 1 < argc ? strtol(argv[++i], &end, 10) : 0;
            if (!end || *end != '\0' || kb < DEFAULT_MEMORY_KB || kb > MAX_MEMORY_KB) {
                fprintf(stderr, "vm: memory size must be between %d and %d KB\n", DEFAULT_MEMORY_KB, MAX_MEMORY_KB);
                return -1;
            }
            opts->memory_size = (uint32_t)kb * 1024;
        } else {
            fprintf(stderr, "vm: unknown option '%s'\n", arg);
            print_usage(stderr, argv[0]);
            return -1;
        }
    }

    if (!opts->program) {
        print_usage(stderr, argv[0]);
        return -1;
    }
    return 1;
}

static void describe_address(const VM *vm, uint32_t address, char *out, size_t size);

static void report_fault(const VM *vm) {
    uint32_t pc = vm->error_pc;

    fprintf(stderr, "vm: error: %s\n", vm_get_error_message(vm));
    char where[80];
    describe_address(vm, pc, where, sizeof(where));
    fprintf(stderr, "vm: at 0x%04X%s%s", pc, where[0] ? " " : "", where);
    const SourceLine *line = debug_line_at(vm->debug_info, pc);
    if (line && line->address == pc) {
        fprintf(stderr, " %s:%u", line->source_file ? line->source_file : "?", line->line_num);
    }

    Instruction instr;
    if (pc % 4 == 0 && vm_peek_instruction(vm, pc, &instr)) {
        char text[160];
        disasm_format(&instr, vm->debug_info, text, sizeof(text));
        fprintf(stderr, ": %s", text);
    }
    fprintf(stderr, "\n");
}

static void describe_address(const VM *vm, uint32_t address, char *out, size_t size) {
    const Symbol *sym = debug_symbol_near(vm->debug_info, address);
    if (!sym) {
        out[0] = '\0';
    } else if (sym->address == address) {
        snprintf(out, size, "<%s>", sym->name);
    } else {
        snprintf(out, size, "<%s+%u>", sym->name, address - sym->address);
    }
}

// Runs like vm_run but prints each instruction on stderr before executing it
static int run_traced(VM *vm) {
    while (!vm->halted) {
        uint32_t pc = vm->registers[R3_PC];
        Instruction instr;
        char where[80], text[160] = "?";

        if (vm_peek_instruction(vm, pc, &instr)) {
            disasm_format(&instr, vm->debug_info, text, sizeof(text));
        }
        describe_address(vm, pc, where, sizeof(where));
        fflush(stdout);
        fprintf(stderr, "0x%04X %-20s %s\n", pc, where, text);

        int result = vm_step(vm);
        if (result != VM_ERROR_NONE) {
            return result;
        }
    }
    return VM_ERROR_NONE;
}

int main(int argc, char *argv[]) {
    Options opts;
    int parsed = parse_options(argc, argv, &opts);
    if (parsed <= 0) {
        return parsed == 0 ? 0 : 1;
    }

    if (opts.disassemble) {
        return disassemble_file(opts.program);
    }

    VM vm;
    if (vm_init(&vm, opts.memory_size) != VM_ERROR_NONE) {
        fprintf(stderr, "vm: cannot initialize: %s\n", vm_get_error_message(&vm));
        vm_cleanup(&vm);
        return 1;
    }
    vm.debug_mode = (uint8_t)opts.debug;
    vm.arg_count = opts.arg_count;
    vm.args = opts.args;

    if (vm_load_program_file(&vm, opts.program) != VM_ERROR_NONE) {
        fprintf(stderr, "vm: cannot load %s: %s\n", opts.program, vm_get_error_message(&vm));
        vm_cleanup(&vm);
        return 1;
    }
    if (opts.verbose) {
        fprintf(stderr, "vm: loaded %s, %u KB memory, entry 0x%04X, %u symbols\n", opts.program,
                vm.memory_size / 1024, vm.registers[R3_PC], vm.debug_info ? vm.debug_info->symbol_count : 0);
    }

    int result = opts.debug ? debugger_run(&vm) : opts.trace ? run_traced(&vm) : vm_run(&vm);
    fflush(stdout);

    if (result != VM_ERROR_NONE && !opts.debug) {
        report_fault(&vm);
    }
    if (opts.verbose) {
        fprintf(stderr, "vm: %s after %u instructions\n", result == VM_ERROR_NONE ? "stopped" : "faulted",
                vm.instruction_count);
    }

    int status = result == VM_ERROR_NONE ? (int)(vm.exit_code & 0xFF) : 1;
    vm_cleanup(&vm);
    return status;
}
