#ifndef _IO_H_
#define _IO_H_

#include "vm_types.h"

#define IO_PORT_CONSOLE 0x00
#define IO_PORT_TIMER   0x40
#define IO_PORT_DISPLAY 0x50

typedef struct IODevice IODevice;

struct IODevice {
    const char *name;
    uint16_t base_port;
    uint16_t port_count;
    uint32_t (*read)(VM *vm, IODevice *device, uint16_t offset);
    void (*write)(VM *vm, IODevice *device, uint16_t offset, uint32_t value);
    void (*tick)(VM *vm, IODevice *device);     // runs after every instruction while the device ticks
    void (*cleanup)(VM *vm, IODevice *device);
    void *state;
    int index;
};

int io_init(VM *vm);
void io_cleanup(VM *vm);

// Port accesses fault when no device claims the port
uint32_t io_read(VM *vm, uint32_t port);
void io_write(VM *vm, uint32_t port, uint32_t value);

// Lets devices advance once per executed instruction
void io_tick(VM *vm);

#endif // _IO_H_
