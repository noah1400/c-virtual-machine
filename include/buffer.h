#ifndef _BUFFER_H_
#define _BUFFER_H_

#include <stddef.h>
#include <stdint.h>

// A growing byte array for writing files; after a failed allocation it ignores further writes
typedef struct Buffer {
    uint8_t *data;
    size_t size;
    size_t capacity;
    int failed;
} Buffer;

void buffer_put(Buffer *b, const void *bytes, size_t count);
void buffer_u8(Buffer *b, uint8_t value);
void buffer_u16(Buffer *b, uint16_t value);
void buffer_u32(Buffer *b, uint32_t value);
// Strings are stored with a 16-bit length and no terminator; NULL is written as an empty string
void buffer_string(Buffer *b, const char *text);
// Writes the buffer to a file; returns 0 when that fails or the buffer did
int buffer_save(const Buffer *b, const char *path);

// Reads little-endian values and strings; once it runs past the end, ok stays 0
typedef struct {
    const uint8_t *ptr;
    const uint8_t *end;
    int ok;
} Reader;

uint32_t reader_uint(Reader *r, int bytes);
// Returns a copy of a string, or NULL for an empty one
char *reader_string(Reader *r);

#endif // _BUFFER_H_
