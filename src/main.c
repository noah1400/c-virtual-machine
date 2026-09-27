#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"
#include "debug.h"
#include "debugger.h"
#include "disassembler.h"
#include "io.h"
#include "monitor.h"
#include "vm.h"

#define DEFAULT_MEMORY_KB 1024
#define MIN_MEMORY_KB     (VM_MIN_MEMORY_SIZE / 1024)
#define MAX_MEMORY_KB     1048576
#define MAX_HISTORY       1000000
#define MAX_LOGPOINTS     32

typedef struct {
    const char *program;
    const char *disk;
    const char *commands;
    uint32_t history;
    int text_display;
    const char *keys;
    const char *logpoints[MAX_LOGPOINTS];
    int logpoint_count;
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
    fprintf(out, "  -k FILE   Take keyboard input from a key script that releases keys at instruction counts\n");
    fprintf(out, "  -L SPEC   Print values each time execution reaches a location, as in -L 'loop:R8,[count]:d'\n");
    fprintf(out, "  -m KB     Memory size in KB, %d to %d (default %d)\n", MIN_MEMORY_KB, MAX_MEMORY_KB,
            DEFAULT_MEMORY_KB);
    fprintf(out, "  -n COUNT  Stop with an error after COUNT instructions\n");
    fprintf(out, "  -p        Print an execution profile by label on stderr\n");
    fprintf(out, "  -s        Print display frames as plain text instead of drawing them\n");
    fprintf(out, "  -t        Trace every executed instruction on stderr\n");
    fprintf(out, "  -v        Report loading and execution statistics on stderr\n");
    fprintf(out, "  -x FILE   Start the debugger and run its commands from FILE\n");
    fprintf(out, "  -H COUNT  Show the last COUNT instructions when the program stops with an error\n");
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
        } else if (strcmp(arg, "-s") == 0) {
            opts->text_display = 1;
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
        } else if (strcmp(arg, "-H") == 0) {
            char *end = NULL;
            unsigned long count = i + 1 < argc ? strtoul(argv[++i], &end, 10) : 0;
            if (!end || *end != '\0' || count == 0 || count > MAX_HISTORY) {
                fprintf(stderr, "vm: the history must hold between 1 and %d instructions\n", MAX_HISTORY);
                return -1;
            }
            opts->history = (uint32_t)count;
        } else if (strcmp(arg, "-k") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "vm: -k needs a key script\n");
                return -1;
            }
            opts->keys = argv[++i];
        } else if (strcmp(arg, "-L") == 0) {
            if (i + 1 >= argc || opts->logpoint_count == MAX_LOGPOINTS) {
                fprintf(stderr, "vm: -L needs a location and takes up to %d of them\n", MAX_LOGPOINTS);
                return -1;
            }
            opts->logpoints[opts->logpoint_count++] = argv[++i];
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
    fprintf(stderr, "vm: error: %s\n", vm_get_error_message(vm));
    fprintf(stderr, "vm: at ");
    cpu_print_location(vm, stderr, vm->error_pc);
    fprintf(stderr, "\n");
    cpu_print_backtrace(vm, stderr, "vm: ");
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
    if (opts.text_display) {
        io_display_as_text(&vm);
    }
    if (opts.keys && io_keyboard_script(&vm, opts.keys) != VM_ERROR_NONE) {
        fprintf(stderr, "vm: cannot read keys from %s\n", vm_get_error_message(&vm));
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

    Monitor monitor = { .trace = opts.trace, .history_size = opts.history };
    if (opts.profile && !opts.debug) {
        monitor.counts = calloc(vm.code_end / 4 + 1, sizeof(uint32_t));
    }
    if (opts.history && !opts.debug) {
        monitor.history = calloc(opts.history, sizeof(HistoryEntry));
    }
    for (int i = 0; i < opts.logpoint_count; i++) {
        char error[160];
        if (monitor_add_logpoint(&monitor, &vm, opts.logpoints[i], error, sizeof(error))) {
            fprintf(stderr, "vm: bad logpoint '%s': %s\n", opts.logpoints[i], error);
            monitor_free(&monitor);
            vm_cleanup(&vm);
            return 1;
        }
    }
    if (!opts.debug) {
        vm_catch_signals();
    }
    int result;
    if (opts.commands) {
        FILE *commands = fopen(opts.commands, "r");
        if (!commands) {
            fprintf(stderr, "vm: cannot open %s: %s\n", opts.commands, strerror(errno));
            monitor_free(&monitor);
            vm_cleanup(&vm);
            return 1;
        }
        result = debugger_run(&vm, commands);
        fclose(commands);
    } else {
        result = opts.debug ? debugger_run(&vm, stdin) : monitor_run(&vm, &monitor);
    }
    fflush(stdout);
    // Devices give the terminal back before anything is reported
    io_cleanup(&vm);

    if (result != VM_ERROR_NONE && !opts.debug) {
        monitor_report_history(&vm, &monitor);
        report_fault(&vm);
    }
    if (monitor.counts) {
        monitor_report_profile(&vm, &monitor);
    }
    monitor_free(&monitor);
    if (opts.verbose) {
        fprintf(stderr, "vm: %s after %u instructions\n", result == VM_ERROR_NONE ? "stopped" : "faulted",
                vm.instruction_count);
    }

    int status = result == VM_ERROR_NONE || result == VM_ERROR_SIGNAL ? (int)(vm.exit_code & 0xFF) : 1;
    vm_cleanup(&vm);
    return status;
}
