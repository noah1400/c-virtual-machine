#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asm.h"

static char *copy_string(const char *text, size_t length) {
    char *copy = malloc(length + 1);
    if (copy) {
        memcpy(copy, text, length);
        copy[length] = '\0';
    }
    return copy;
}

static char *read_text(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }

    size_t capacity = 4096, length = 0;
    char *text = malloc(capacity);
    while (text) {
        if (length + 1 >= capacity) {
            char *grown = realloc(text, capacity * 2);
            if (!grown) {
                free(text);
                text = NULL;
                break;
            }
            text = grown;
            capacity *= 2;
        }
        size_t n = fread(text + length, 1, capacity - length - 1, file);
        if (n == 0) {
            break;
        }
        length += n;
    }
    fclose(file);

    if (text) {
        text[length] = '\0';
    }
    return text;
}

static int file_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file) {
        fclose(file);
    }
    return file != NULL;
}

static int is_absolute(const char *path) {
    return path[0] == '/' || path[0] == '\\' || (path[0] && path[1] == ':');
}

static char *join_path(const char *dir, size_t dir_length, const char *name) {
    char *path = malloc(dir_length + strlen(name) + 2);
    if (path) {
        memcpy(path, dir, dir_length);
        path[dir_length] = '\0';
        if (dir_length > 0 && dir[dir_length - 1] != '/' && dir[dir_length - 1] != '\\') {
            strcat(path, "/");
        }
        strcat(path, name);
    }
    return path;
}

// Include paths are relative to the including file, then to each -I directory
static char *resolve_include(Assembler *as, const char *includer, const char *name) {
    if (is_absolute(name)) {
        return copy_string(name, strlen(name));
    }

    const char *slash = strrchr(includer, '/');
    const char *backslash = strrchr(includer, '\\');
    if (backslash && (!slash || backslash > slash)) {
        slash = backslash;
    }
    char *path = join_path(includer, slash ? (size_t)(slash - includer + 1) : 0, name);
    if (path && file_exists(path)) {
        return path;
    }
    free(path);

    for (int i = 0; i < as->include_dir_count; i++) {
        path = join_path(as->include_dirs[i], strlen(as->include_dirs[i]), name);
        if (path && file_exists(path)) {
            return path;
        }
        free(path);
    }
    return NULL;
}

static int add_line(Assembler *as, LineKind kind, const char *file, int number, char *text) {
    if (as->line_count == as->line_capacity) {
        size_t capacity = as->line_capacity ? as->line_capacity * 2 : 256;
        SourceLine *lines = realloc(as->lines, capacity * sizeof(SourceLine));
        if (!lines) {
            free(text);
            return 0;
        }
        as->lines = lines;
        as->line_capacity = capacity;
    }
    as->lines[as->line_count++] = (SourceLine){ kind, file, number, text };
    return 1;
}

static const char *add_file(Assembler *as, char *path) {
    char **files = realloc(as->files, (as->file_count + 1) * sizeof(char *));
    if (!files) {
        free(path);
        return NULL;
    }
    as->files = files;
    as->files[as->file_count++] = path;
    return path;
}

// Finds the file named by an .include directive, skipping any labels in front of it
static int include_target(const char *text, char *out, size_t size) {
    TokenList tokens;
    char error[64];
    int found = 0;

    if (!lex_line(text, &tokens, error, sizeof(error))) {
        return 0;
    }

    int i = 0;
    while (tokens.items[i].kind == TOK_IDENT && token_is_punct(&tokens.items[i + 1], ':')) {
        i += 2;
    }
    if (tokens.items[i].kind == TOK_IDENT && name_equals(tokens.items[i].text, ".include") &&
        tokens.items[i + 1].kind == TOK_STRING) {
        snprintf(out, size, "%s", tokens.items[i + 1].text);
        found = 1;
    }

    tokens_free(&tokens);
    return found;
}

static int load_file(Assembler *as, char *path, const char **stack, int depth) {
    char *text = read_text(path);
    if (!text) {
        asm_error(as, "cannot read %s", path);
        free(path);
        return 0;
    }

    const char *file = add_file(as, path);
    if (!file) {
        free(text);
        return 0;
    }
    stack[depth] = file;

    int ok = 1;
    int number = 0;
    char *line = text;
    while (ok && *line) {
        char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        if (length > 0 && line[length - 1] == '\r') {
            length--;
        }

        char *copy = copy_string(line, length);
        number++;
        if (!copy || !add_line(as, LINE_SOURCE, file, number, copy)) {
            ok = 0;
            break;
        }

        char target[512];
        if (include_target(copy, target, sizeof(target))) {
            size_t index = as->line_count - 1;
            char *resolved = resolve_include(as, file, target);

            as->line = &as->lines[index];
            if (!resolved) {
                asm_error(as, "cannot find include file \"%s\"", target);
            } else if (depth + 1 >= ASM_MAX_INCLUDE_DEPTH) {
                asm_error(as, "includes are nested too deeply");
                free(resolved);
            } else {
                int cycle = 0;
                for (int i = 0; i <= depth; i++) {
                    cycle |= strcmp(stack[i], resolved) == 0;
                }
                if (cycle) {
                    asm_error(as, "\"%s\" includes itself", resolved);
                    free(resolved);
                } else {
                    ok = add_line(as, LINE_INCLUDE_BEGIN, file, number, NULL) &&
                         load_file(as, resolved, stack, depth + 1) &&
                         add_line(as, LINE_INCLUDE_END, file, number, NULL);
                }
            }
            as->line = NULL;
        }

        line = end ? end + 1 : line + length;
    }

    free(text);
    return ok;
}

int source_load(Assembler *as, const char *path) {
    const char *stack[ASM_MAX_INCLUDE_DEPTH];
    char *copy = copy_string(path, strlen(path));
    return copy && load_file(as, copy, stack, 0) && as->errors == 0;
}
