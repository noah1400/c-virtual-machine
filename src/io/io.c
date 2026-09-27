#include <stdio.h>
#include <stdlib.h>
#include "devices.h"
#include "vm.h"

#define MAX_IO_DEVICES 8

struct IODevices {
    IODevice devices[MAX_IO_DEVICES];
    int count;
};

// Devices that count instructions only get ticks while they need them
void io_set_ticking(VM *vm, const IODevice *device, int ticking) {
    if (ticking) {
        vm->io_ticking |= 1u << device->index;
    } else {
        vm->io_ticking &= ~(1u << device->index);
    }
}

static int add_device(VM *vm, IODevice device) {
    struct IODevices *io = vm->io_devices;
    if (io->count >= MAX_IO_DEVICES) {
        return vm_raise(vm, VM_ERROR_IO_ERROR, "Too many I/O devices");
    }
    device.index = io->count;
    io->devices[io->count++] = device;
    return VM_ERROR_NONE;
}

int io_init(VM *vm) {
    static int (*const constructors[])(VM *, IODevice *) = {
        console_device, timer_device, display_device, keyboard_device,
    };

    vm->io_devices = calloc(1, sizeof(struct IODevices));
    if (!vm->io_devices) {
        return vm_raise(vm, VM_ERROR_MEMORY_ALLOCATION, "Failed to allocate I/O devices");
    }
    for (size_t i = 0; i < sizeof(constructors) / sizeof(constructors[0]); i++) {
        IODevice device;
        if (constructors[i](vm, &device) != VM_ERROR_NONE) {
            return vm->last_error;
        }
        if (add_device(vm, device) != VM_ERROR_NONE) {
            free(device.state);
            return vm->last_error;
        }
    }
    return VM_ERROR_NONE;
}

void io_cleanup(VM *vm) {
    struct IODevices *io = vm->io_devices;
    if (!io) {
        return;
    }
    for (int i = 0; i < io->count; i++) {
        if (io->devices[i].cleanup) {
            io->devices[i].cleanup(vm, &io->devices[i]);
        }
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
        if ((vm->io_ticking >> i) & 1) {
            io->devices[i].tick(vm, &io->devices[i]);
        }
    }
}
