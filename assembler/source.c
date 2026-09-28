#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asm.h"

#define MAX_EXPANSION_DEPTH 32
#define MAX_MACRO_ARGUMENTS 16
#define MAX_REPEATS         65536

// A .rept block whose lines are being collected
typedef struct {
    int64_t count;
    int depth;                  // .rept lines inside it that are still open
    char **lines;
    int *numbers;
    int line_count;
} Repeat;

typedef struct {
    const char *includes[ASM_MAX_INCLUDE_DEPTH];
    int include_depth;
    Macro *defining;            // macro whose body is being collected
    Repeat *repeating;
    int expansion_depth;
    SymbolTable constants;      // .equ constants that can be evaluated while reading, for .rept counts
} Loader;

static int handle_line(Assembler *as, Loader *loader, const char *file, int number, char *text, int expanded);

static char *copy_string(const char *text, size_t length) {
    char *copy = malloc(length + 1);
    if (copy) {
        memcpy(copy, text, length);
        copy[length] = '\0';
    }
    return copy;
}

char *read_text(const char *path) {
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
char *source_find(Assembler *as, const char *includer, const char *name) {
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

static int add_line(Assembler *as, LineKind kind, const char *file, int number, char *text, int expanded) {
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
    as->lines[as->line_count++] = (SourceLine){ kind, file, number, text, expanded };
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

// Reports an error at the most recently added line
static void loader_error(Assembler *as, const char *message, const char *detail) {
    as->line = &as->lines[as->line_count - 1];
    asm_error(as, message, detail);
    as->line = NULL;
}

static Macro *find_macro(Assembler *as, const char *name) {
    for (size_t i = 0; i < as->macro_count; i++) {
        if (name_equals(as->macros[i].name, name)) {
            return &as->macros[i];
        }
    }
    return NULL;
}

static int load_file(Assembler *as, Loader *loader, char *path) {
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
    loader->includes[loader->include_depth] = file;

    int ok = 1;
    int number = 0;
    for (char *line = text; ok && *line;) {
        char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        size_t kept = length > 0 && line[length - 1] == '\r' ? length - 1 : length;
        char *copy = copy_string(line, kept);

        number++;
        ok = copy && handle_line(as, loader, file, number, copy, 0);
        line = end ? end + 1 : line + length;
    }

    free(text);
    return ok;
}

static int include_file(Assembler *as, Loader *loader, const char *file, int number, const char *name) {
    char *resolved = source_find(as, file, name);

    if (!resolved) {
        loader_error(as, "cannot find include file \"%s\"", name);
        return 1;
    }
    if (loader->include_depth + 1 >= ASM_MAX_INCLUDE_DEPTH) {
        loader_error(as, "includes are nested too deeply%s", "");
        free(resolved);
        return 1;
    }
    for (int i = 0; i <= loader->include_depth; i++) {
        if (strcmp(loader->includes[i], resolved) == 0) {
            loader_error(as, "\"%s\" includes itself", resolved);
            free(resolved);
            return 1;
        }
    }

    loader->include_depth++;
    int ok = add_line(as, LINE_INCLUDE_BEGIN, file, number, NULL, 0) && load_file(as, loader, resolved) &&
             add_line(as, LINE_INCLUDE_END, file, number, NULL, 0);
    loader->include_depth--;
    return ok;
}

// Parameters are names, each optionally followed by = and the text of its default argument
static int define_macro(Assembler *as, Loader *loader, const char *text, const TokenList *tokens, int first) {
    const Token *name = &tokens->items[first + 1];

    if (first > 0) {
        loader_error(as, "a macro definition cannot have a label%s", "");
        return 1;
    }
    if (name->kind != TOK_IDENT || name->text[0] == '.') {
        loader_error(as, "expected a macro name%s", "");
        return 1;
    }
    if (isa_by_mnemonic(name->text) || isa_register_index(name->text) >= 0 || find_macro(as, name->text)) {
        loader_error(as, "'%s' is already an instruction, register or macro", name->text);
        return 1;
    }

    Macro *macros = realloc(as->macros, (as->macro_count + 1) * sizeof(Macro));
    if (!macros) {
        return 0;
    }
    as->macros = macros;
    Macro *macro = &as->macros[as->macro_count++];
    memset(macro, 0, sizeof(*macro));
    macro->name = copy_string(name->text, strlen(name->text));
    loader->defining = macro;

    for (int i = first + 2; tokens->items[i].kind != TOK_END; i++) {
        const Token *param = &tokens->items[i];
        char *fallback = NULL;
        int next = i + 1;

        if (param->kind == TOK_IDENT && token_is_punct(&tokens->items[next], '=')) {
            int depth = 0, end = ++next;
            while (tokens->items[end].kind != TOK_END && (depth > 0 || !token_is_punct(&tokens->items[end], ','))) {
                depth += token_is_punct(&tokens->items[end], '(') || token_is_punct(&tokens->items[end], '[');
                depth -= token_is_punct(&tokens->items[end], ')') || token_is_punct(&tokens->items[end], ']');
                end++;
            }
            size_t start = tokens->items[next].start, stop = tokens->items[end].start;
            while (stop > start && (text[stop - 1] == ' ' || text[stop - 1] == '\t')) {
                stop--;
            }
            fallback = copy_string(text + start, end > next ? stop - start : 0);
            if (!fallback) {
                return 0;
            }
            next = end;
        }
        if (param->kind != TOK_IDENT || (tokens->items[next].kind != TOK_END && !token_is_punct(&tokens->items[next], ','))) {
            free(fallback);
            loader_error(as, "expected macro parameter names separated by commas%s", "");
            return 1;
        }
        char **params = realloc(macro->params, (size_t)(macro->param_count + 1) * sizeof(char *));
        char **defaults = params ? realloc(macro->defaults, (size_t)(macro->param_count + 1) * sizeof(char *)) : NULL;
        if (params) {
            macro->params = params;
        }
        if (!defaults) {
            free(fallback);
            return 0;
        }
        macro->defaults = defaults;
        macro->defaults[macro->param_count] = fallback;
        macro->params[macro->param_count++] = copy_string(param->text, strlen(param->text));
        i = tokens->items[next].kind != TOK_END ? next : next - 1;
    }
    return macro->name != NULL;
}

static int append_body_line(Macro *macro, const char *text) {
    char **body = realloc(macro->body, (size_t)(macro->body_count + 1) * sizeof(char *));
    if (!body) {
        return 0;
    }
    macro->body = body;
    macro->body[macro->body_count] = copy_string(text, strlen(text));
    return macro->body[macro->body_count++] != NULL;
}

// Splits text at commas outside of quotes and brackets
static int split_arguments(const char *text, char **arguments, int max) {
    int count = 0, depth = 0;
    char quote = 0;
    const char *start = text;

    while (*text == ' ' || *text == '\t') {
        text++;
    }
    if (*text == '\0') {
        return 0;
    }
    for (const char *p = start;; p++) {
        if (quote) {
            if (*p == '\\' && p[1]) {
                p++;
            } else if (*p == quote) {
                quote = 0;
            }
        } else if (*p == '"' || *p == '\'') {
            quote = *p;
        } else if (*p == '[' || *p == '(') {
            depth++;
        } else if (*p == ']' || *p == ')') {
            depth--;
        }

        if (*p == '\0' || (*p == ',' && depth == 0 && !quote)) {
            if (count == max) {
                return -1;
            }
            const char *a = start, *b = p;
            while (a < b && isspace((unsigned char)*a)) {
                a++;
            }
            while (b > a && isspace((unsigned char)b[-1])) {
                b--;
            }
            arguments[count++] = copy_string(a, (size_t)(b - a));
            if (*p == '\0') {
                return count;
            }
            start = p + 1;
        }
    }
}

// Replaces \param with its argument and \@ with the expansion number, outside of quotes
static char *substitute(const Macro *macro, char **arguments, const char *line, unsigned id) {
    size_t capacity = strlen(line) + 64, length = 0;
    char *out = malloc(capacity);
    char quote = 0;

    for (const char *p = line; out && *p;) {
        char piece[32];
        const char *insert = NULL;
        size_t skip = 1;

        if (quote) {
            if (*p == '\\' && p[1]) {
                skip = 2;
            } else if (*p == quote) {
                quote = 0;
            }
        } else if (*p == '"' || *p == '\'') {
            quote = *p;
        } else if (*p == '\\' && p[1] == '@') {
            snprintf(piece, sizeof(piece), "%u", id);
            insert = piece;
            skip = 2;
        } else if (*p == '\\') {
            for (int i = 0; i < macro->param_count; i++) {
                size_t n = strlen(macro->params[i]);
                if (strncmp(p + 1, macro->params[i], n) == 0 && !isalnum((unsigned char)p[1 + n]) && p[1 + n] != '_') {
                    insert = arguments[i];
                    skip = n + 1;
                    break;
                }
            }
        }

        size_t add = insert ? strlen(insert) : skip;
        if (length + add + 1 > capacity) {
            capacity = (length + add + 1) * 2;
            char *grown = realloc(out, capacity);
            if (!grown) {
                free(out);
                return NULL;
            }
            out = grown;
        }
        memcpy(out + length, insert ? insert : p, add);
        length += add;
        p += skip;
    }

    if (out) {
        out[length] = '\0';
    }
    return out;
}

// Missing trailing arguments and empty ones take the parameter's default
static int expand_macro(Assembler *as, Loader *loader, Macro *macro, const char *file, int number, const char *args) {
    char *arguments[MAX_MACRO_ARGUMENTS], *values[MAX_MACRO_ARGUMENTS];
    int count = split_arguments(args, arguments, MAX_MACRO_ARGUMENTS);
    int required = 0, ok = 1;

    for (int i = 0; i < macro->param_count; i++) {
        if (!macro->defaults[i]) {
            required = i + 1;
        }
    }
    for (int i = 0; i < macro->param_count && count <= macro->param_count; i++) {
        values[i] = i < count ? arguments[i] : NULL;
        if ((!values[i] || !values[i][0]) && macro->defaults[i]) {
            values[i] = macro->defaults[i];
        }
    }

    if (count < required || count > macro->param_count) {
        char detail[160];
        if (required == macro->param_count) {
            snprintf(detail, sizeof(detail), "%s expects %d argument%s", macro->name, macro->param_count,
                     macro->param_count == 1 ? "" : "s");
        } else {
            snprintf(detail, sizeof(detail), "%s expects %d to %d arguments", macro->name, required,
                     macro->param_count);
        }
        loader_error(as, "%s", detail);
    } else if (loader->expansion_depth >= MAX_EXPANSION_DEPTH) {
        loader_error(as, "macro %s is expanded too deeply", macro->name);
    } else {
        unsigned id = as->expansions++;
        loader->expansion_depth++;
        for (int i = 0; ok && i < macro->body_count; i++) {
            char *text = substitute(macro, values, macro->body[i], id);
            ok = text && handle_line(as, loader, file, number, text, 1);
        }
        loader->expansion_depth--;
    }

    for (int i = 0; i < count; i++) {
        free(arguments[i]);
    }
    return ok;
}

// Evaluates an expression from the tokens at first, knowing only earlier constants; returns 0 when
// it cannot be evaluated yet
static int evaluate_now(Assembler *as, Loader *loader, TokenList *tokens, int first, int quiet, int64_t *value) {
    Parser p = { .as = as, .tokens = tokens, .pos = first, .constants = &loader->constants, .quiet = quiet };
    *value = parse_expression(&p);
    return !p.failed && !p.unresolved && tokens->items[p.pos].kind == TOK_END;
}

// Remembers a constant whose value is known while the file is read
static void note_constant(Assembler *as, Loader *loader, TokenList *tokens, int first) {
    const Token *name = &tokens->items[first + 1];
    int64_t value;

    if (name->kind != TOK_IDENT || !token_is_punct(&tokens->items[first + 2], ',') ||
        symbols_find(&loader->constants, name->text) || !evaluate_now(as, loader, tokens, first + 3, 1, &value)) {
        return;
    }
    AsmSymbol *sym = symbols_add(&loader->constants, name->text);
    if (sym) {
        sym->kind = SYM_CONST;
        sym->value = value;
        sym->defined = 1;
    }
}

static void free_repeat(Repeat *repeat) {
    for (int i = 0; repeat && i < repeat->line_count; i++) {
        free(repeat->lines[i]);
    }
    if (repeat) {
        free(repeat->lines);
        free(repeat->numbers);
        free(repeat);
    }
}

static int start_repeat(Assembler *as, Loader *loader, TokenList *tokens, int first) {
    int64_t count;

    if (first > 0) {
        loader_error(as, "a .rept line cannot have a label%s", "");
        return 1;
    }
    int errors = as->errors;
    as->line = &as->lines[as->line_count - 1];
    int known = evaluate_now(as, loader, tokens, first + 1, 0, &count);
    as->line = NULL;
    if (!known) {
        if (as->errors == errors) {
            loader_error(as, "the .rept count must be a constant defined before it%s", "");
        }
        return 1;
    }
    if (count < 0 || count > MAX_REPEATS) {
        loader_error(as, "the .rept count must be between 0 and 65536%s", "");
        return 1;
    }
    loader->repeating = calloc(1, sizeof(Repeat));
    if (!loader->repeating) {
        return 0;
    }
    loader->repeating->count = count;
    return 1;
}

static int collect_repeat_line(Repeat *repeat, const char *text, int number) {
    char **lines = realloc(repeat->lines, (size_t)(repeat->line_count + 1) * sizeof(char *));
    int *numbers = lines ? realloc(repeat->numbers, (size_t)(repeat->line_count + 1) * sizeof(int)) : NULL;
    if (lines) {
        repeat->lines = lines;
    }
    if (!numbers) {
        return 0;
    }
    repeat->numbers = numbers;
    repeat->numbers[repeat->line_count] = number;
    repeat->lines[repeat->line_count] = copy_string(text, strlen(text));
    return repeat->lines[repeat->line_count++] != NULL;
}

// Hands the lines of a finished block to handle_line count times, where inner blocks repeat again
static int expand_repeat(Assembler *as, Loader *loader, Repeat *repeat, const char *file) {
    int ok = 1;
    for (int64_t n = 0; ok && n < repeat->count; n++) {
        for (int i = 0; ok && i < repeat->line_count; i++) {
            char *copy = copy_string(repeat->lines[i], strlen(repeat->lines[i]));
            ok = copy && handle_line(as, loader, file, repeat->numbers[i], copy, 1);
        }
    }
    free_repeat(repeat);
    return ok;
}

// Adds a line to the program, collecting macro bodies and splicing in includes and expansions
static int handle_line(Assembler *as, Loader *loader, const char *file, int number, char *text, int expanded) {
    TokenList tokens;
    char error[64];

    if (!lex_line(text, &tokens, error, sizeof(error))) {
        // Macro bodies only become valid once their parameters are substituted; elsewhere pass 1 reports it
        if (loader->defining) {
            return append_body_line(loader->defining, text) &&
                   add_line(as, LINE_MACRO_DEFINITION, file, number, text, expanded);
        }
        return add_line(as, LINE_SOURCE, file, number, text, expanded);
    }

    int first = 0;
    while (tokens.items[first].kind == TOK_IDENT && token_is_punct(&tokens.items[first + 1], ':')) {
        first += 2;
    }
    const Token *keyword = &tokens.items[first];
    const char *word = keyword->kind == TOK_IDENT ? keyword->text : "";
    int ok = 1;

    if (loader->repeating && !loader->defining) {
        Repeat *repeat = loader->repeating;
        ok = add_line(as, LINE_MACRO_DEFINITION, file, number, text, expanded);
        if (ok && name_equals(word, ".endr") && repeat->depth == 0) {
            loader->repeating = NULL;
            tokens_free(&tokens);
            return expand_repeat(as, loader, repeat, file);
        }
        repeat->depth += name_equals(word, ".rept") - name_equals(word, ".endr");
        ok = ok && collect_repeat_line(repeat, text, number);
        tokens_free(&tokens);
        return ok;
    }

    if (loader->defining) {
        Macro *macro = loader->defining;
        if (name_equals(word, ".endm")) {
            loader->defining = NULL;
        } else if (name_equals(word, ".macro")) {
            ok = add_line(as, LINE_MACRO_DEFINITION, file, number, text, expanded);
            loader_error(as, "macro definitions cannot be nested%s", "");
            tokens_free(&tokens);
            return ok;
        } else {
            ok = append_body_line(macro, text);
        }
        tokens_free(&tokens);
        return ok && add_line(as, LINE_MACRO_DEFINITION, file, number, text, expanded);
    }

    if (name_equals(word, ".macro")) {
        ok = add_line(as, LINE_MACRO_DEFINITION, file, number, text, expanded) &&
             define_macro(as, loader, text, &tokens, first);
    } else if (name_equals(word, ".endm")) {
        ok = add_line(as, LINE_SOURCE, file, number, text, expanded);
        loader_error(as, ".endm without .macro%s", "");
    } else if (name_equals(word, ".rept")) {
        ok = add_line(as, LINE_MACRO_DEFINITION, file, number, text, expanded) &&
             start_repeat(as, loader, &tokens, first);
    } else if (name_equals(word, ".endr")) {
        ok = add_line(as, LINE_SOURCE, file, number, text, expanded);
        loader_error(as, ".endr without .rept%s", "");
    } else if (name_equals(word, ".include") && tokens.items[first + 1].kind == TOK_STRING) {
        ok = add_line(as, LINE_SOURCE, file, number, text, expanded) &&
             include_file(as, loader, file, number, tokens.items[first + 1].text);
    } else if (keyword->kind == TOK_IDENT && find_macro(as, word)) {
        Macro *macro = find_macro(as, word);
        const char *args = text + (tokens.items[first + 1].kind == TOK_END ? tokens.end : tokens.items[first + 1].start);
        char *copy = copy_string(args, tokens.end - (size_t)(args - text));
        ok = copy && add_line(as, LINE_MACRO_CALL, file, number, text, expanded) &&
             expand_macro(as, loader, macro, file, number, copy);
        free(copy);
    } else {
        if (name_equals(word, ".equ") || name_equals(word, ".set")) {
            note_constant(as, loader, &tokens, first);
        }
        ok = add_line(as, LINE_SOURCE, file, number, text, expanded);
    }

    tokens_free(&tokens);
    return ok;
}

int source_load(Assembler *as, const char *path) {
    Loader loader = { 0 };
    char *copy = copy_string(path, strlen(path));

    symbols_init(&loader.constants);
    int ok = copy && load_file(as, &loader, copy);

    if (ok && loader.defining) {
        loader_error(as, "macro %s is missing .endm", loader.defining->name);
    }
    if (ok && loader.repeating) {
        loader_error(as, ".rept is missing .endr%s", "");
    }
    free_repeat(loader.repeating);
    symbols_free(&loader.constants);
    return ok && as->errors == 0;
}
