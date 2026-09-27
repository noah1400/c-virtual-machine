#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include "cpu.h"
#include "devices.h"
#include "vm.h"

#define KEYBOARD_QUEUE_SIZE    64
#define KEYBOARD_POLL_INTERVAL 10000    // instructions between polls while key interrupts are on

#define KEYBOARD_KEY_WAITING 0x01
#define KEYBOARD_INPUT_ENDED 0x02

// Keys are the bytes read from stdin, except that the arrow keys become 0x100 (up), 0x101 (down),
// 0x102 (right) and 0x103 (left) and other escape sequences are dropped
#define KEY_ESCAPE 0x1B
#define KEY_UP     0x100

typedef struct {
    uint16_t keys[KEYBOARD_QUEUE_SIZE];
    int first, count;
    int ended;
    int claimed;
    int raw;                // the terminal settings below have to be restored
    struct termios saved;
    uint8_t vector;
    uint32_t countdown;
} KeyboardState;

// The first use of the keyboard stops the terminal from buffering lines and echoing keys
static void keyboard_claim(KeyboardState *keyboard) {
    if (keyboard->claimed) {
        return;
    }
    keyboard->claimed = 1;
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &keyboard->saved) == 0) {
        struct termios settings = keyboard->saved;
        settings.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
        settings.c_cc[VMIN] = 1;
        settings.c_cc[VTIME] = 0;
        keyboard->raw = tcsetattr(STDIN_FILENO, TCSANOW, &settings) == 0;
    }
}

// Returns the next byte of input if one is waiting, or -1
static int next_byte(KeyboardState *keyboard) {
    struct pollfd input = { .fd = STDIN_FILENO, .events = POLLIN };
    unsigned char byte;

    if (keyboard->ended || poll(&input, 1, 0) <= 0) {
        return -1;
    }
    ssize_t length = read(STDIN_FILENO, &byte, 1);
    if (length == 1) {
        return byte;
    }
    if (length == 0 || (errno != EINTR && errno != EAGAIN)) {
        keyboard->ended = 1;
    }
    return -1;
}

static void add_key(KeyboardState *keyboard, int key) {
    if (keyboard->count < KEYBOARD_QUEUE_SIZE) {
        keyboard->keys[(keyboard->first + keyboard->count++) % KEYBOARD_QUEUE_SIZE] = (uint16_t)key;
    }
}

// Moves waiting input into the queue; a terminal sends a whole escape sequence at once, so an
// escape that nothing follows yet is the Escape key
static void keyboard_poll(KeyboardState *keyboard) {
    int byte;
    while (keyboard->count < KEYBOARD_QUEUE_SIZE && (byte = next_byte(keyboard)) >= 0) {
        if (byte != KEY_ESCAPE) {
            add_key(keyboard, byte);
            continue;
        }
        int kind = next_byte(keyboard);
        if (kind != '[' && kind != 'O') {
            add_key(keyboard, KEY_ESCAPE);
            if (kind >= 0) {
                add_key(keyboard, kind);
            }
            continue;
        }
        int final = next_byte(keyboard);
        while (final >= 0x20 && final < 0x40) {
            final = next_byte(keyboard);
        }
        if (final >= 'A' && final <= 'D') {
            add_key(keyboard, KEY_UP + final - 'A');
        }
    }
}

// Port 0 reads the status bits, port 1 the next key (0 if none waits) and port 2 holds the
// interrupt vector that is requested while keys wait (0 for none)
static uint32_t keyboard_read(VM *vm, IODevice *device, uint16_t offset) {
    KeyboardState *keyboard = device->state;
    (void)vm;
    keyboard_claim(keyboard);
    if (keyboard->count == 0 && offset < 2) {
        keyboard_poll(keyboard);
    }
    switch (offset) {
        case 0:
            return (keyboard->count ? KEYBOARD_KEY_WAITING : 0) | (keyboard->ended ? KEYBOARD_INPUT_ENDED : 0);
        case 1: {
            if (keyboard->count == 0) {
                return 0;
            }
            uint16_t key = keyboard->keys[keyboard->first];
            keyboard->first = (keyboard->first + 1) % KEYBOARD_QUEUE_SIZE;
            keyboard->count--;
            return key;
        }
        case 2:
            return keyboard->vector;
        default:
            return 0;
    }
}

static void keyboard_write(VM *vm, IODevice *device, uint16_t offset, uint32_t value) {
    KeyboardState *keyboard = device->state;
    keyboard_claim(keyboard);
    if (offset == 2) {
        keyboard->vector = (uint8_t)value;
        keyboard->countdown = KEYBOARD_POLL_INTERVAL;
        io_set_ticking(vm, device, keyboard->vector != 0);
    }
}

static void keyboard_tick(VM *vm, IODevice *device) {
    KeyboardState *keyboard = device->state;
    if (--keyboard->countdown) {
        return;
    }
    keyboard->countdown = KEYBOARD_POLL_INTERVAL;
    keyboard_poll(keyboard);
    if (keyboard->count) {
        cpu_request_interrupt(vm, keyboard->vector);
    } else if (keyboard->ended) {
        io_set_ticking(vm, device, 0);
    }
}

static void keyboard_cleanup(VM *vm, IODevice *device) {
    KeyboardState *keyboard = device->state;
    (void)vm;
    if (keyboard->raw) {
        tcsetattr(STDIN_FILENO, TCSADRAIN, &keyboard->saved);
        keyboard->raw = 0;
    }
}

int keyboard_device(VM *vm, IODevice *device) {
    *device = (IODevice){ .name = "keyboard", .base_port = IO_PORT_KEYBOARD, .port_count = 3,
                          .read = keyboard_read, .write = keyboard_write, .tick = keyboard_tick,
                          .cleanup = keyboard_cleanup, .state = calloc(1, sizeof(KeyboardState)) };
    return device->state ? VM_ERROR_NONE : vm_raise(vm, VM_ERROR_MEMORY_ALLOCATION, "Failed to allocate the keyboard");
}
