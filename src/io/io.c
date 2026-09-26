#include <stdio.h>
#include <stdlib.h>
#include "io.h"
#include "vm.h"

#define MAX_IO_DEVICES 8

struct IODevices {
    IODevice devices[MAX_IO_DEVICES];
    int count;
};

// Console: port 0 reads a character (0 at end of input) and writes stdout, port 1 writes stderr
static uint32_t console_read(VM *vm, IODevice *device, uint16_t offset) {
    (void)vm;
    (void)device;
    if (offset != 0) {
        return 0;
    }
    int c = getchar();
    return c == EOF ? 0 : (uint32_t)c;
}

static void console_write(VM *vm, IODevice *device, uint16_t offset, uint32_t value) {
    (void)vm;
    (void)device;
    FILE *stream = offset == 0 ? stdout : stderr;
    fputc((int)(value & 0xFF), stream);
    fflush(stream);
}

static int add_device(VM *vm, IODevice device) {
    struct IODevices *io = vm->io_devices;
    if (io->count >= MAX_IO_DEVICES) {
        return vm_raise(vm, VM_ERROR_IO_ERROR, "Too many I/O devices");
    }
    io->devices[io->count++] = device;
    return VM_ERROR_NONE;
}

int io_init(VM *vm) {
    vm->io_devices = calloc(1, sizeof(struct IODevices));
    if (!vm->io_devices) {
        return vm_raise(vm, VM_ERROR_MEMORY_ALLOCATION, "Failed to allocate I/O devices");
    }

    IODevice console = { .name = "console", .base_port = 0x00, .port_count = 2,
                         .read = console_read, .write = console_write };
    return add_device(vm, console);
}

void io_cleanup(VM *vm) {
    struct IODevices *io = vm->io_devices;
    if (!io) {
        return;
    }
    for (int i = 0; i < io->count; i++) {
        free(io->devices[i].state);
    }
    free(io);
    vm->io_devices = NULL;
}

static IODevice *find_device(VM *vm, uint32_t port) {
    struct IODevices *io = vm->io_devices;
    for (int i = 0; io && i < io->count; i++) {
        IODevice *device = &io->devices[i];
        if (port >= device->base_port && port < (uint32_t)device->base_port + device->port_count) {
            return device;
        }
    }
    vm_raise(vm, VM_ERROR_IO_ERROR, "No device on I/O port 0x%04X", port);
    return NULL;
}

uint32_t io_read(VM *vm, uint32_t port) {
    IODevice *device = find_device(vm, port);
    return device && device->read ? device->read(vm, device, (uint16_t)(port - device->base_port)) : 0;
}

void io_write(VM *vm, uint32_t port, uint32_t value) {
    IODevice *device = find_device(vm, port);
    if (device && device->write) {
        device->write(vm, device, (uint16_t)(port - device->base_port), value);
    }
}

void io_tick(VM *vm) {
    struct IODevices *io = vm->io_devices;
    for (int i = 0; io && i < io->count; i++) {
        if (io->devices[i].tick) {
            io->devices[i].tick(vm, &io->devices[i]);
        }
    }
}
