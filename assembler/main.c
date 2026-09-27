#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asm.h"

static void print_usage(FILE *out, const char *name) {
    fprintf(out, "Usage: %s [options] input.asm\n", name);
    fprintf(out, "Options:\n");
    fprintf(out, "  -o FILE   Write the binary to FILE (default: input with a .bin extension)\n");
    fprintf(out, "  -c        Write an object file for vmld instead (default extension .o)\n");
    fprintf(out, "  -l FILE   Write a listing to FILE\n");
    fprintf(out, "  -I DIR    Also search DIR for included files\n");
    fprintf(out, "  -D NAME[=VALUE]  Define a constant, 1 unless a value is given\n");
    fprintf(out, "  -s        Print the symbol table\n");
    fprintf(out, "  -S        Leave out debug information\n");
    fprintf(out, "  -W        Warn when a call or syscall overwrites a register value that is read afterwards\n");
    fprintf(out, "  -h        Show this help\n");
}

// Replaces the extension of the input file, or appends one
static char *default_output(const char *input, const char *extension) {
    const char *slash = strrchr(input, '/');
    const char *dot = strrchr(input, '.');
    size_t stem = dot && (!slash || dot > slash) ? (size_t)(dot - input) : strlen(input);
    char *path = malloc(stem + strlen(extension) + 1);
    if (path) {
        memcpy(path, input, stem);
        strcpy(path + stem, extension);
    }
    return path;
}

int main(int argc, char *argv[]) {
    const char *input = NULL, *output = NULL, *listing = NULL;
    int show_symbols = 0, with_debug = 1, check = 0;
    Assembler as;

    asm_init(&as);

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        int takes_value = strcmp(arg, "-o") == 0 || strcmp(arg, "-l") == 0 ||
                          strncmp(arg, "-I", 2) == 0 || strncmp(arg, "-D", 2) == 0;
        // -I and -D also accept their value attached, as in -DNAME
        const char *value = takes_value && arg[2] != '\0' ? arg + 2 : NULL;

        if (takes_value && !value) {
            if (i + 1 >= argc) {
                fprintf(stderr, "vmasm: error: %s needs a value\n", arg);
                return 1;
            }
            value = argv[++i];
        }
        if (strcmp(arg, "-o") == 0) {
            output = value;
        } else if (strcmp(arg, "-l") == 0) {
            listing = value;
        } else if (strncmp(arg, "-I", 2) == 0) {
            if (as.include_dir_count == ASM_MAX_INCLUDE_DIRS) {
                fprintf(stderr, "vmasm: error: too many include directories\n");
                return 1;
            }
            as.include_dirs[as.include_dir_count++] = value;
        } else if (strncmp(arg, "-D", 2) == 0) {
            if (as.define_count == ASM_MAX_DEFINES) {
                fprintf(stderr, "vmasm: error: too many -D options\n");
                return 1;
            }
            as.defines[as.define_count++] = value;
        } else if (strcmp(arg, "-s") == 0) {
            show_symbols = 1;
        } else if (strcmp(arg, "-S") == 0) {
            with_debug = 0;
        } else if (strcmp(arg, "-W") == 0) {
            check = 1;
        } else if (strcmp(arg, "-c") == 0) {
            as.object = 1;
        } else if (strcmp(arg, "-h") == 0) {
            print_usage(stdout, argv[0]);
            return 0;
        } else if (arg[0] == '-' && arg[1] != '\0') {
            fprintf(stderr, "vmasm: error: unknown option '%s'\n", arg);
            print_usage(stderr, argv[0]);
            return 1;
        } else if (input) {
            fprintf(stderr, "vmasm: error: only one input file may be given\n");
            return 1;
        } else {
            input = arg;
        }
    }

    if (!input) {
        print_usage(stderr, argv[0]);
        return 1;
    }

    char *output_path = output ? NULL : default_output(input, as.object ? ".o" : ".bin");
    const char *path = output ? output : output_path;
    int ok = asm_assemble(&as, input) && (as.object ? output_object(&as, path) : output_binary(&as, path, with_debug)) &&
             (!listing || output_listing(&as, listing));

    if (ok && show_symbols) {
        output_symbols(&as);
    }
    if (ok && check) {
        check_registers(&as);
    }

    free(output_path);
    asm_free(&as);
    return ok ? 0 : 1;
}
