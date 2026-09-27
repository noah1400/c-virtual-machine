#ifndef _IO_DEVICES_H_
#define _IO_DEVICES_H_

#include "io.h"

// Each device fills in its descriptor and allocates its state; they return a VM error code
int console_device(VM *vm, IODevice *device);
int timer_device(VM *vm, IODevice *device);
int display_device(VM *vm, IODevice *device);
int keyboard_device(VM *vm, IODevice *device);
int disk_device(VM *vm, IODevice *device);
int disk_attach(VM *vm, IODevice *device, const char *path);

void io_set_ticking(VM *vm, const IODevice *device, int ticking);

#endif // _IO_DEVICES_H_
