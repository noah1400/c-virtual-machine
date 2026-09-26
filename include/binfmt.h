#ifndef _BINFMT_H_
#define _BINFMT_H_

#include <stdint.h>

// VM32 binary layout: header, code bytes, data bytes, symbol table. Version 1.0 headers are
// 32 bytes; version 1.1 appends the entry point address.
#define VM32_MAGIC           "VM32"
#define VM32_VERSION_MAJOR   1
#define VM32_VERSION_MINOR   1
#define VM32_MIN_HEADER_SIZE 32
#define VM32_HEADER_SIZE     36
#define VM32_MAX_FILE_SIZE   (16u * 1024 * 1024)

typedef struct {
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t header_size;
    uint32_t code_base;
    uint32_t code_size;
    uint32_t data_base;
    uint32_t data_size;
    uint32_t symbol_size;
    uint32_t entry;
    const uint8_t *code;
    const uint8_t *data;
    const uint8_t *symbols;
} Vm32Image;

static inline uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void write_le16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static inline void write_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

int vm32_is_image(const uint8_t *image, uint32_t size);

// Returns NULL on success, otherwise a description of the problem
const char *vm32_parse(const uint8_t *image, uint32_t size, Vm32Image *out);

// Reads a whole file followed by a NUL byte; returns NULL and sets *error on failure
uint8_t *read_binary_file(const char *path, uint32_t *size, const char **error);

#endif // _BINFMT_H_
