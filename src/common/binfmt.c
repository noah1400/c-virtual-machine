#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "binfmt.h"

int vm32_is_image(const uint8_t *image, uint32_t size) {
    return size >= 4 && memcmp(image, VM32_MAGIC, 4) == 0;
}

const char *vm32_parse(const uint8_t *image, uint32_t size, Vm32Image *out) {
    if (!vm32_is_image(image, size)) {
        return "Not a VM32 binary";
    }
    if (size < VM32_HEADER_SIZE) {
        return "Truncated VM32 header";
    }

    out->version_major = read_le16(image + 4);
    out->version_minor = read_le16(image + 6);
    out->header_size = read_le32(image + 8);
    out->code_base = read_le32(image + 12);
    out->code_size = read_le32(image + 16);
    out->data_base = read_le32(image + 20);
    out->data_size = read_le32(image + 24);
    out->symbol_size = read_le32(image + 28);

    if (out->version_major != VM32_VERSION_MAJOR) {
        return "Unsupported VM32 format version";
    }
    if (out->header_size < VM32_HEADER_SIZE || out->header_size > size) {
        return "Invalid header size in program file";
    }
    out->entry = read_le32(image + 32);
    if ((uint64_t)out->header_size + out->code_size + out->data_size + out->symbol_size > size) {
        return "Segment sizes exceed program file size";
    }

    out->code = image + out->header_size;
    out->data = out->code + out->code_size;
    out->symbols = out->data + out->data_size;
    return NULL;
}

uint8_t *read_binary_file(const char *path, uint32_t *size, const char **error) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        *error = "Failed to open file";
        return NULL;
    }

    long length = -1;
    if (fseek(file, 0, SEEK_END) == 0) {
        length = ftell(file);
        rewind(file);
    }
    if (length < 0 || (unsigned long)length > VM32_MAX_FILE_SIZE) {
        fclose(file);
        *error = "File is unreadable or too large";
        return NULL;
    }

    uint8_t *buffer = malloc((size_t)length + 1);
    if (!buffer) {
        fclose(file);
        *error = "Out of memory";
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, (size_t)length, file);
    fclose(file);
    if (bytes_read != (size_t)length) {
        free(buffer);
        *error = "Failed to read file";
        return NULL;
    }

    buffer[length] = 0;
    *size = (uint32_t)length;
    return buffer;
}
