#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "ore.h"

void *allocate(size_t size) {
    void *p = calloc(1, size ? size : 1);
    if (!p) {
        fprintf(stderr, "vmc0: error: out of memory\n");
        exit(1);
    }
    return p;
}

char *copy_text(const char *text, size_t length) {
    char *copy = allocate(length + 1);
    memcpy(copy, text, length);
    return copy;
}

// Resizes an array to hold count items; the caller tracks how many are used
void grow(void **items, int count, size_t item_size) {
    void *p = realloc(*items, (size_t)count * item_size);
    if (!p) {
        fprintf(stderr, "vmc0: error: out of memory\n");
        exit(1);
    }
    *items = p;
}

// Makes room for one more item after the used ones, zeroing the new room
void extend(void **items, int *capacity, int used, size_t item_size) {
    if (used < *capacity) {
        return;
    }
    int larger = *capacity ? *capacity * 2 : 8;
    grow(items, larger, item_size);
    memset((char *)*items + (size_t)*capacity * item_size, 0, (size_t)(larger - *capacity) * item_size);
    *capacity = larger;
}

_Noreturn void fail(const Module *m, int line, const char *format, ...) {
    va_list args;
    if (m) {
        fprintf(stderr, "%s:%d: error: ", m->path, line);
    } else {
        fprintf(stderr, "vmc0: error: ");
    }
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    exit(1);
}

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    size_t capacity = 4096, size = 0;
    char *text = allocate(capacity);
    for (size_t n; (n = fread(text + size, 1, capacity - size - 1, file)) > 0;) {
        size += n;
        if (size + 1 == capacity) {
            capacity *= 2;
            grow((void **)&text, (int)capacity, 1);
        }
    }
    int failed = ferror(file);
    fclose(file);
    if (failed) {
        free(text);
        return NULL;
    }
    text[size] = '\0';
    if (strlen(text) != size) {
        fail(NULL, 0, "%s contains a NUL byte", path);
    }
    return text;
}

static int exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file) {
        fclose(file);
    }
    return file != NULL;
}

static char *join(const char *dir, size_t dir_length, const char *name, const char *extension) {
    size_t length = dir_length + strlen(name) + strlen(extension) + 2;
    char *path = allocate(length);
    if (dir_length == 0) {
        snprintf(path, length, "%s%s", name, extension);
    } else if (dir[dir_length - 1] == '/') {
        snprintf(path, length, "%.*s%s%s", (int)dir_length, dir, name, extension);
    } else {
        snprintf(path, length, "%.*s/%s%s", (int)dir_length, dir, name, extension);
    }
    return path;
}

// An import names a file without .ore, next to the importing file, in an -I directory or in the library
static char *find_import(Program *program, const char *name, const Module *importer) {
    const char *slash = strrchr(importer->path, '/');
    char *path = join(importer->path, slash ? (size_t)(slash - importer->path + 1) : 0, name, ".ore");
    if (exists(path)) {
        return path;
    }
    for (int i = 0; i < program->include_dir_count; i++) {
        path = join(program->include_dirs[i], strlen(program->include_dirs[i]), name, ".ore");
        if (exists(path)) {
            return path;
        }
    }
    path = join(program->library, strlen(program->library), name, ".ore");
    return exists(path) ? path : NULL;
}

static void add_assembly(Program *program, const char *path) {
    extend((void **)&program->assembly, &program->assembly_capacity, program->assembly_count, sizeof(char *));
    program->assembly[program->assembly_count++] = path;
}

static const char *module_name(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char *dot = strrchr(base, '.');
    size_t length = dot && dot != base ? (size_t)(dot - base) : strlen(base);
    char *name = copy_text(base, length);
    int valid = isalpha((unsigned char)name[0]) || name[0] == '_';
    for (size_t i = 0; i < length; i++) {
        valid &= isalnum((unsigned char)name[i]) || name[i] == '_';
    }
    return valid ? name : NULL;
}

Module *load_module(Program *program, const char *path, const Module *importer, int line) {
    char *resolved = importer ? find_import(program, path, importer) : copy_text(path, strlen(path));
    if (!resolved) {
        fail(importer, line, "cannot find the module \"%s\"", path);
    }
    for (int i = 0; i < program->module_count; i++) {
        if (strcmp(program->modules[i]->path, resolved) == 0) {
            return program->modules[i];
        }
    }

    Module *m = allocate(sizeof(Module));
    m->path = resolved;
    m->name = module_name(resolved);
    if (!m->name) {
        fail(importer, line, "the file name of %s is not a valid module name", resolved);
    }
    for (int i = 0; i < program->module_count; i++) {
        if (strcmp(program->modules[i]->name, m->name) == 0) {
            fail(importer, line, "%s and %s are both named %s", program->modules[i]->path, resolved, m->name);
        }
    }
    m->source = read_file(resolved);
    if (!m->source) {
        fail(importer, line, "cannot read %s", resolved);
    }
    grow((void **)&program->modules, program->module_count + 1, sizeof(Module *));
    program->modules[program->module_count++] = m;

    // An imported module can bring assembly for its extern functions in a file of the same name. The
    // main module cannot, since that is where -S puts the program's assembly.
    size_t length = strlen(resolved);
    if (importer && length > 4 && strcmp(resolved + length - 4, ".ore") == 0) {
        char *assembly = copy_text(resolved, length);
        strcpy(assembly + length - 4, ".asm");
        if (exists(assembly)) {
            add_assembly(program, assembly);
        }
    }
    lex(m);
    parse(program, m);
    return m;
}

static void usage(FILE *out) {
    fprintf(out, "Usage: vmc0 [options] program.ore [file.asm...]\n"
                 "Compiles an Ore program and the modules it imports into a VM32 binary, together with the\n"
                 "assembly files that define its extern functions.\n"
                 "  -o FILE   write the binary to FILE (default: the program with .bin)\n"
                 "  -S        write the assembly instead, to FILE or the program with .asm\n"
                 "  -g0       leave out the .loc lines and the binary's debug information\n"
                 "  -b ADDR   put the program at ADDR instead of 0, as vmasm -b does\n"
                 "  -m KB     ask for KB of memory, as vmasm -m does\n"
                 "  -I DIR    look for imported modules in DIR as well\n"
                 "  -L DIR    take the runtime and the standard library from DIR\n"
                 "  -h        show this help\n");
}

static char *concat(const char *a, size_t a_length, const char *b) {
    char *text = allocate(a_length + strlen(b) + 1);
    memcpy(text, a, a_length);
    strcpy(text + a_length, b);
    return text;
}

// The source path with its extension replaced
static char *derived(const char *path, const char *extension) {
    const char *slash = strrchr(path, '/');
    const char *dot = strrchr(path, '.');
    size_t length = dot && (!slash || dot > slash) ? (size_t)(dot - path) : strlen(path);
    return concat(path, length, extension);
}

static int write_file(const char *path, const char *text, size_t size) {
    FILE *file = fopen(path, "wb");
    if (!file) {
        return 0;
    }
    int ok = fwrite(text, 1, size, file) == size;
    return fclose(file) == 0 && ok;
}

// Runs vmasm from the directory vmc0 lives in, or else from the PATH. Assembly files given with
// relative paths are found from the current directory.
static int assemble(const char *self, const char *library, const char *base, const char *memory,
                    const char *source, const char *output, int no_debug) {
    const char *slash = strrchr(self, '/');
    char *vmasm = slash ? join(self, (size_t)(slash - self), "vmasm", "") : copy_text("vmasm", 5);
    char *args[14] = { vmasm, "-I", (char *)library, "-I", ".", (char *)source, "-o", (char *)output, "-b",
                       (char *)base };
    int count = 10;
    if (memory) {
        args[count++] = "-m";
        args[count++] = (char *)memory;
    }
    if (no_debug) {
        args[count++] = "-S";
    }

    fflush(stdout);
    pid_t child = fork();
    if (child == 0) {
        if (slash) {
            execv(vmasm, args);
        }
        execvp("vmasm", args);
        fprintf(stderr, "vmc0: error: cannot run vmasm\n");
        _exit(127);
    }
    int status;
    return child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int main(int argc, char **argv) {
    Program program = { 0 };
    const char *source = NULL, *output = NULL, *base = "0", *memory = NULL;
    int assembly_only = 0;

    // The runtime and the standard library live in ore/lib next to vmc0
    const char *slash = strrchr(argv[0], '/');
    program.library = join(argv[0], slash ? (size_t)(slash - argv[0]) : 0, "ore/lib", "");
    program.include_dirs = allocate((size_t)argc * sizeof(char *));

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(arg, "-S") == 0) {
            assembly_only = 1;
        } else if (strcmp(arg, "-g0") == 0) {
            program.no_loc = 1;
        } else if ((strcmp(arg, "-o") == 0 || strcmp(arg, "-I") == 0 || strcmp(arg, "-L") == 0 ||
                    strcmp(arg, "-b") == 0 || strcmp(arg, "-m") == 0) && i + 1 < argc) {
            if (arg[1] == 'o') {
                output = argv[++i];
            } else if (arg[1] == 'b') {
                base = argv[++i];
            } else if (arg[1] == 'm') {
                memory = argv[++i];
            } else if (arg[1] == 'I') {
                program.include_dirs[program.include_dir_count++] = argv[++i];
            } else {
                program.library = argv[++i];
            }
        } else if (arg[0] == '-') {
            usage(stderr);
            return 1;
        } else if (strlen(arg) > 4 && strcmp(arg + strlen(arg) - 4, ".asm") == 0) {
            add_assembly(&program, arg);
        } else if (source) {
            usage(stderr);
            return 1;
        } else {
            source = arg;
        }
    }
    if (!source) {
        usage(stderr);
        return 1;
    }
    load_module(&program, source, NULL, 0);
    check_program(&program);

    size_t size;
    char *assembly = generate(&program, &size);
    if (!output) {
        output = derived(source, assembly_only ? ".asm" : ".bin");
    }
    char *path = assembly_only ? (char *)output : concat(output, strlen(output), ".asm");
    if (!write_file(path, assembly, size)) {
        fail(NULL, 0, "cannot write %s", path);
    }
    if (assembly_only) {
        return 0;
    }
    if (!assemble(argv[0], program.library, base, memory, path, output, program.no_loc)) {
        fprintf(stderr, "vmc0: error: vmasm could not assemble %s\n", path);
        return 1;
    }
    remove(path);
    return 0;
}
