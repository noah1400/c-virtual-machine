#include <stdlib.h>
#include "cpu.h"
#include "devices.h"
#include "vm.h"

// Timer: port 0 sets the interval in instructions (0 stops it), port 1 the interrupt vector,
// port 2 counts expirations; every expiration requests an interrupt
typedef struct {
    uint32_t interval;
    uint32_t counter;
    uint32_t ticks;
    uint8_t vector;
} TimerState;

static uint32_t timer_read(VM *vm, IODevice *device, uint16_t offset) {
    (void)vm;
    TimerState *timer = device->state;
    switch (offset) {
        case 0:
            return timer->interval;
        case 1:
            return timer->vector;
        default:
            return timer->ticks;
    }
}

static void timer_write(VM *vm, IODevice *device, uint16_t offset, uint32_t value) {
    TimerState *timer = device->state;
    switch (offset) {
        case 0:
            timer->interval = value;
            timer->counter = 0;
            io_set_ticking(vm, device, value != 0);
            break;
        case 1:
            if (value > 0xFF) {
                vm_raise(vm, VM_ERROR_IO_ERROR, "Invalid timer interrupt vector: %u", value);
            }
            timer->vector = (uint8_t)value;
            break;
        default:
            timer->ticks = value;
            break;
    }
}

static void timer_tick(VM *vm, IODevice *device) {
    TimerState *timer = device->state;
    if (timer->interval != 0 && ++timer->counter >= timer->interval) {
        timer->counter = 0;
        timer->ticks++;
        cpu_request_interrupt(vm, timer->vector);
    }
}

int timer_device(VM *vm, IODevice *device) {
    *device = (IODevice){ .name = "timer", .base_port = IO_PORT_TIMER, .port_count = 3,
                          .read = timer_read, .write = timer_write, .tick = timer_tick,
                          .state = calloc(1, sizeof(TimerState)) };
    return device->state ? VM_ERROR_NONE : vm_raise(vm, VM_ERROR_MEMORY_ALLOCATION, "Failed to allocate the timer");
}
