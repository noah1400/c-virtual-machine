#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "buffer.h"

void buffer_put(Buffer *b, const void *bytes, size_t count) {
    if (b->failed || count == 0) {
        return;
    }
    if (b->size + count > b->capacity) {
        size_t capacity = b->capacity ? b->capacity : 4096;
        while (capacity < b->size + count) {
            capacity *= 2;
        }
        uint8_t *data = realloc(b->data, capacity);
        if (!data) {
            b->failed = 1;
            return;
        }
        b->data = data;
        b->capacity = capacity;
    }
    memcpy(b->data + b->size, bytes, count);
    b->size += count;
}

void buffer_u8(Buffer *b, uint8_t value) {
    buffer_put(b, &value, 1);
}

void buffer_u16(Buffer *b, uint16_t value) {
    uint8_t bytes[2] = { (uint8_t)value, (uint8_t)(value >> 8) };
    buffer_put(b, bytes, 2);
}

void buffer_u32(Buffer *b, uint32_t value) {
    uint8_t bytes[4] = { (uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24) };
    buffer_put(b, bytes, 4);
}

void buffer_string(Buffer *b, const char *text) {
    size_t length = text ? strlen(text) : 0;
    if (length > 0xFFFF) {
        length = 0xFFFF;
    }
    buffer_u16(b, (uint16_t)length);
    buffer_put(b, text, length);
}

int buffer_save(const Buffer *b, const char *path) {
    FILE *file = b->failed ? NULL : fopen(path, "wb");
    int ok = file && fwrite(b->data, 1, b->size, file) == b->size;
    if (file && fclose(file) != 0) {
        ok = 0;
    }
    return ok;
}

uint32_t reader_uint(Reader *r, int bytes) {
    if (!r->ok || r->end - r->ptr < bytes) {
        r->ok = 0;
        return 0;
    }
    uint32_t value = 0;
    for (int i = 0; i < bytes; i++) {
        value |= (uint32_t)r->ptr[i] << (8 * i);
    }
    r->ptr += bytes;
    return value;
}

char *reader_string(Reader *r) {
    uint16_t length = (uint16_t)reader_uint(r, 2);
    if (!r->ok || r->end - r->ptr < length) {
        r->ok = 0;
        return NULL;
    }
    if (length == 0) {
        return NULL;
    }
    char *text = malloc(length + 1u);
    if (!text) {
        r->ok = 0;
        return NULL;
    }
    memcpy(text, r->ptr, length);
    text[length] = '\0';
    r->ptr += length;
    return text;
}
