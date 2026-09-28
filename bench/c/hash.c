#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Counts keys in a chained hash table: allocation, hashing and pointer chasing
typedef struct Entry {
    int key;
    int count;
    struct Entry *next;
} Entry;

#define BUCKETS 65521

// FNV-1a over the four bytes of the key
static uint32_t hash(int key) {
    uint32_t h = 2166136261u;
    uint32_t k = (uint32_t)key;
    for (int i = 0; i < 4; i += 1) {
        h ^= k & 255;
        h *= 16777619u;
        k >>= 8;
    }
    return h;
}

static Entry *find(Entry **table, int key) {
    Entry *e = table[hash(key) % BUCKETS];
    while (e != NULL && e->key != key) {
        e = e->next;
    }
    return e;
}

int main(void) {
    Entry **table = calloc(BUCKETS, sizeof(Entry *));
    int distinct = 0;
    for (int i = 0; i < 4000000; i += 1) {
        int key = i % 100003 * 7919 % 100003;
        Entry *e = find(table, key);
        if (e == NULL) {
            int bucket = (int)(hash(key) % BUCKETS);
            e = calloc(1, sizeof(Entry));
            e->key = key;
            e->next = table[bucket];
            table[bucket] = e;
            distinct += 1;
        }
        e->count += 1;
    }
    printf("%d %d %d\n", distinct, find(table, 12345)->count, find(table, 99999)->count);
    return 0;
}
