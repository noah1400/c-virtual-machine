#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "debugger.h"
#include "disassembler.h"
#include "io.h"
#include "vm.h"

#define DEFAULT_MEMORY_KB 1024
#define MIN_MEMORY_KB     (VM_MIN_MEMORY_SIZE / 1024)
#define MAX_MEMORY_KB     1048576

typedef struct {
    const char *program;
    const char *disk;
    const char *commands;
    int arg_count;
    char **args;
    uint32_t memory_size;
    uint32_t instruction_limit;
    int debug;
    int disassemble;
    int trace;
    int profile;
    int verbose;
} Options;

static void print_usage(FILE *out, const char *name) {
    fprintf(out, "Usage: %s [options] program.bin [arguments...]\n", name);
    fprintf(out, "Options:\n");
    fprintf(out, "  -b FILE   Attach FILE as the disk image\n");
    fprintf(out, "  -d        Start the interactive debugger\n");
    fprintf(out, "  -D        Disassemble the program instead of running it\n");
    fprintf(out, "  -m KB     Memory size in KB, %d to %d (default %d)\n", MIN_MEMORY_KB, MAX_MEMORY_KB,
            DEFAULT_MEMORY_KB);
    fprintf(out, "  -n COUNT  Stop with an error after COUNT instructions\n");
    fprintf(out, "  -p        Print an execution profile by label on stderr\n");
    fprintf(out, "  -t        Trace every executed instruction on stderr\n");
    fprintf(out, "  -v        Report loading and execution statistics on stderr\n");
    fprintf(out, "  -x FILE   Start the debugger and run its commands from FILE\n");
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
        } else if (strcmp(arg, "-b") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "vm: -b needs a disk image\n");
                return -1;
            }
            opts->disk = argv[++i];
        } else if (strcmp(arg, "-d") == 0) {
            opts->debug = 1;
        } else if (strcmp(arg, "-D") == 0) {
            opts->disassemble = 1;
        } else if (strcmp(arg, "-t") == 0) {
            opts->trace = 1;
        } else if (strcmp(arg, "-p") == 0) {
            opts->profile = 1;
        } else if (strcmp(arg, "-v") == 0) {
            opts->verbose = 1;
        } else if (strcmp(arg, "-x") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "vm: -x needs a command file\n");
                return -1;
            }
            opts->commands = argv[++i];
            opts->debug = 1;
        } else if (strcmp(arg, "-h") == 0) {
            print_usage(stdout, argv[0]);
            return 0;
        } else if (strcmp(arg, "-m") == 0) {
            char *end = NULL;
            long kb = i + 1 < argc ? strtol(argv[++i], &end, 10) : 0;
            if (!end || *end != '\0' || kb < MIN_MEMORY_KB || kb > MAX_MEMORY_KB) {
                fprintf(stderr, "vm: memory size must be between %d and %d KB\n", MIN_MEMORY_KB, MAX_MEMORY_KB);
                return -1;
            }
            opts->memory_size = (uint32_t)kb * 1024;
        } else if (strcmp(arg, "-n") == 0) {
            char *end = NULL;
            unsigned long long count = i + 1 < argc ? strtoull(argv[++i], &end, 10) : 0;
            if (!end || *end != '\0' || count == 0 || count > UINT32_MAX) {
                fprintf(stderr, "vm: the instruction limit must be between 1 and %u\n", UINT32_MAX);
                return -1;
            }
            opts->instruction_limit = (uint32_t)count;
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

static void report_fault(const VM *vm) {
    uint32_t pc = vm->error_pc;

    fprintf(stderr, "vm: error: %s\n", vm_get_error_message(vm));
    char where[80];
    debug_describe(vm->debug_info, pc, where, sizeof(where));
    fprintf(stderr, "vm: at 0x%04X%s%s", pc, where[0] ? " " : "", where);
    const SourceLine *line = debug_line_at(vm->debug_info, pc);
    if (line && line->address == pc) {
        fprintf(stderr, " %s:%u", line->source_file ? line->source_file : "?", line->line_num);
    }

    Instruction instr;
    if (pc % 4 == 0 && vm_peek_instruction(vm, pc, &instr)) {
        char text[160];
        disasm_format(&instr, pc, vm->debug_info, text, sizeof(text));
        fprintf(stderr, ": %s", text);
    }
    fprintf(stderr, "\n");
}

// Runs like vm_run, optionally printing each instruction on stderr before executing it and
// counting how often each instruction of the loaded code executes
static int run(VM *vm, int trace, uint32_t *counts) {
    while (!vm->halted) {
        if (trace) {
            uint32_t pc = vm->registers[R3_PC];
            Instruction instr;
            char where[80], text[160] = "?";

            if (vm_peek_instruction(vm, pc, &instr)) {
                disasm_format(&instr, pc, vm->debug_info, text, sizeof(text));
            }
            debug_describe(vm->debug_info, pc, where, sizeof(where));
            fflush(stdout);
            fprintf(stderr, "0x%04X %-20s %s\n", pc, where, text);
        }

        int result = vm_step(vm);
        if (result != VM_ERROR_NONE) {
            return result;
        }
        if (trace && vm->exception) {
            fprintf(stderr, "vm: exception %u: %s\n", vm->exception, vm->exception_message);
        }
        if (counts && vm->error_pc < vm->code_end) {
            counts[vm->error_pc / 4]++;
        }
    }
    return VM_ERROR_NONE;
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
static void report_profile(const VM *vm, const uint32_t *counts) {
    enum { SHOWN = 15 };
    uint32_t slots = (vm->code_end + 3) / 4;
    ProfileEntry *entries = calloc(slots ? slots : 1, sizeof(ProfileEntry));
    size_t used = 0;
    uint64_t total = 0;

    if (!entries) {
        return;
    }
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
    vm.instruction_limit = opts.instruction_limit;
    vm.arg_count = opts.arg_count;
    vm.args = opts.args;

    if (vm_load_program_file(&vm, opts.program) != VM_ERROR_NONE) {
        fprintf(stderr, "vm: cannot load %s: %s\n", opts.program, vm_get_error_message(&vm));
        vm_cleanup(&vm);
        return 1;
    }
    if (opts.disk && io_attach_disk(&vm, opts.disk) != VM_ERROR_NONE) {
        fprintf(stderr, "vm: cannot attach the disk image %s\n", vm_get_error_message(&vm));
        vm_cleanup(&vm);
        return 1;
    }
    if (opts.verbose) {
        fprintf(stderr, "vm: loaded %s, %u KB memory, entry 0x%04X, %u symbols\n", opts.program,
                vm.memory_size / 1024, vm.registers[R3_PC], vm.debug_info ? vm.debug_info->symbol_count : 0);
    }

    uint32_t *counts = opts.profile && !opts.debug ? calloc(vm.code_end / 4 + 1, sizeof(uint32_t)) : NULL;
    if (!opts.debug) {
        vm_catch_signals();
    }
    int result;
    if (opts.commands) {
        FILE *commands = fopen(opts.commands, "r");
        if (!commands) {
            fprintf(stderr, "vm: cannot open %s: %s\n", opts.commands, strerror(errno));
            vm_cleanup(&vm);
            return 1;
        }
        result = debugger_run(&vm, commands);
        fclose(commands);
    } else {
        result = opts.debug ? debugger_run(&vm, stdin) : run(&vm, opts.trace, counts);
    }
    fflush(stdout);
    // Devices give the terminal back before anything is reported
    io_cleanup(&vm);

    if (result != VM_ERROR_NONE && !opts.debug) {
        report_fault(&vm);
    }
    if (counts) {
        report_profile(&vm, counts);
        free(counts);
    }
    if (opts.verbose) {
        fprintf(stderr, "vm: %s after %u instructions\n", result == VM_ERROR_NONE ? "stopped" : "faulted",
                vm.instruction_count);
    }

    int status = result == VM_ERROR_NONE || result == VM_ERROR_SIGNAL ? (int)(vm.exit_code & 0xFF) : 1;
    vm_cleanup(&vm);
    return status;
}
