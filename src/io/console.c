#include <stdio.h>
#include "devices.h"

// Console: port 0 reads a character (0 at end of input) and writes stdout, port 1 writes stderr
static uint32_t console_read(VM *vm, IODevice *device, uint16_t offset) {
    (void)vm;
    (void)device;
    if (offset != 0) {
        return 0;
    }
    fflush(stdout);
    int c = getchar();
    return c == EOF ? 0 : (uint32_t)c;
}

static void console_write(VM *vm, IODevice *device, uint16_t offset, uint32_t value) {
    (void)vm;
    (void)device;
    if (offset != 0) {
        fflush(stdout);
    }
    fputc((int)(value & 0xFF), offset == 0 ? stdout : stderr);
}

int console_device(VM *vm, IODevice *device) {
    (void)vm;
    *device = (IODevice){ .name = "console", .base_port = IO_PORT_CONSOLE, .port_count = 2,
                          .read = console_read, .write = console_write };
    return VM_ERROR_NONE;
}
