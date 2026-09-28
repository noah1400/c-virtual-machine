#include <stdlib.h>
#include <string.h>
#include "asm.h"

static size_t hash_name(const char *name) {
    size_t hash = 2166136261u;
    while (*name) {
        hash = (hash ^ (unsigned char)*name++) * 16777619u;
    }
    return hash;
}

void symbols_init(SymbolTable *table) {
    memset(table, 0, sizeof(*table));
}

void symbols_free(SymbolTable *table) {
    for (size_t i = 0; i < table->count; i++) {
        free(table->items[i].name);
    }
    free(table->items);
    free(table->buckets);
    memset(table, 0, sizeof(*table));
}

AsmSymbol *symbols_find(SymbolTable *table, const char *name) {
    if (table->bucket_count == 0) {
        return NULL;
    }
    for (size_t i = hash_name(name) % table->bucket_count; table->buckets[i]; i = (i + 1) % table->bucket_count) {
        AsmSymbol *sym = &table->items[table->buckets[i] - 1];
        if (strcmp(sym->name, name) == 0) {
            return sym;
        }
    }
    return NULL;
}

static int rehash(SymbolTable *table, size_t bucket_count) {
    size_t *buckets = calloc(bucket_count, sizeof(size_t));
    if (!buckets) {
        return 0;
    }
    for (size_t n = 0; n < table->count; n++) {
        size_t i = hash_name(table->items[n].name) % bucket_count;
        while (buckets[i]) {
            i = (i + 1) % bucket_count;
        }
        buckets[i] = n + 1;
    }
    free(table->buckets);
    table->buckets = buckets;
    table->bucket_count = bucket_count;
    return 1;
}

// Removes the symbols that removed marks, keeping the order of the others. Constants that alias an
// external symbol follow it to its new index.
int symbols_remove(SymbolTable *table, const unsigned char *removed) {
    size_t *index = malloc(table->count * sizeof(size_t) + 1);
    if (!index) {
        return 0;
    }
    size_t count = 0;
    for (size_t i = 0; i < table->count; i++) {
        if (removed[i]) {
            free(table->items[i].name);
            continue;
        }
        index[i] = count;
        table->items[count++] = table->items[i];
    }
    table->count = count;
    for (size_t i = 0; i < count; i++) {
        if (table->items[i].base >= BASE_SYMBOL) {
            table->items[i].base = BASE_SYMBOL + (int)index[table->items[i].base - BASE_SYMBOL];
        }
    }
    free(index);
    return rehash(table, table->bucket_count);
}

// Adds an undefined symbol; the caller must check that the name is not present yet
AsmSymbol *symbols_add(SymbolTable *table, const char *name) {
    if (table->count == table->capacity) {
        size_t capacity = table->capacity ? table->capacity * 2 : 64;
        AsmSymbol *items = realloc(table->items, capacity * sizeof(AsmSymbol));
        if (!items) {
            return NULL;
        }
        table->items = items;
        table->capacity = capacity;
    }

    AsmSymbol *sym = &table->items[table->count];
    memset(sym, 0, sizeof(*sym));
    sym->name = malloc(strlen(name) + 1);
    if (!sym->name) {
        return NULL;
    }
    strcpy(sym->name, name);
    table->count++;

    // Keep the table at most half full
    if (table->count * 2 > table->bucket_count && !rehash(table, table->bucket_count ? table->bucket_count * 2 : 128)) {
        table->count--;
        free(sym->name);
        return NULL;
    }
    if (table->bucket_count && symbols_find(table, name) != sym) {
        size_t i = hash_name(name) % table->bucket_count;
        while (table->buckets[i]) {
            i = (i + 1) % table->bucket_count;
        }
        table->buckets[i] = table->count;
    }
    return sym;
}
