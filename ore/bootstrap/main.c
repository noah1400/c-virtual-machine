#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    lex(m);
    parse(program, m);
    return m;
}

static void usage(FILE *out) {
    fprintf(out, "Usage: vmc0 [options] program.ore\n"
                 "Reads an Ore program and the modules it imports.\n"
                 "  -I DIR    look for imported modules in DIR as well\n"
                 "  -L DIR    take the standard library from DIR\n"
                 "  -h        show this help\n");
}

int main(int argc, char **argv) {
    Program program = { 0 };
    const char *source = NULL;

    // The runtime and the standard library live in ore/lib next to vmc0
    const char *slash = strrchr(argv[0], '/');
    program.library = join(argv[0], slash ? (size_t)(slash - argv[0]) : 0, "ore/lib", "");
    program.include_dirs = allocate((size_t)argc * sizeof(char *));

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0) {
            usage(stdout);
            return 0;
        } else if ((strcmp(arg, "-I") == 0 || strcmp(arg, "-L") == 0) && i + 1 < argc) {
            if (arg[1] == 'I') {
                program.include_dirs[program.include_dir_count++] = argv[++i];
            } else {
                program.library = argv[++i];
            }
        } else if (arg[0] == '-' || source) {
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
    return 0;
}
