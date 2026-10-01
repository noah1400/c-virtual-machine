#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "binfmt.h"
#include "cpu.h"
#include "devices.h"
#include "vm.h"

#define KEYBOARD_QUEUE_SIZE    64
#define KEYBOARD_POLL_INTERVAL 10000    // instructions between polls while key interrupts are on

#define KEYBOARD_KEY_WAITING 0x01
#define KEYBOARD_INPUT_ENDED 0x02

// Keys are the bytes read from stdin, except that the escape sequences of the arrow and editing keys
// become the codes from 0x100 on, and other escape sequences are dropped
#define KEY_ESCAPE    0x1B
#define KEY_UP        0x100
#define KEY_HOME      0x104
#define KEY_END       0x105
#define KEY_INSERT    0x106
#define KEY_DELETE    0x107
#define KEY_PAGE_UP   0x108
#define KEY_PAGE_DOWN 0x109

// Keys of a script that arrive once the VM has executed a number of instructions
typedef struct {
    uint32_t at;
    uint32_t start, length;     // where the keys lie in the script's bytes
} KeyEvent;

typedef struct {
    uint16_t keys[KEYBOARD_QUEUE_SIZE];
    int first, count;
    int ended;
    int claimed;
    int raw;                // the terminal settings below have to be restored
    struct termios saved;
    uint8_t vector;
    uint32_t countdown;
    KeyEvent *events;       // a key script that replaces stdin, or NULL
    uint8_t *script;
    uint32_t event_count, next_event, event_offset;
} KeyboardState;

// Without a script, the first use of the keyboard stops the terminal from buffering lines, echoing
// keys and keeping control keys other than Ctrl-C for itself
static void keyboard_claim(KeyboardState *keyboard) {
    if (keyboard->claimed || keyboard->events) {
        return;
    }
    keyboard->claimed = 1;
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &keyboard->saved) == 0) {
        struct termios settings = keyboard->saved;
        settings.c_lflag &= ~(tcflag_t)(ICANON | ECHO | IEXTEN);
        settings.c_iflag &= ~(tcflag_t)IXON;
        settings.c_cc[VMIN] = 1;
        settings.c_cc[VTIME] = 0;
        settings.c_cc[VSUSP] = _POSIX_VDISABLE;
        settings.c_cc[VQUIT] = _POSIX_VDISABLE;
        keyboard->raw = tcsetattr(STDIN_FILENO, TCSANOW, &settings) == 0;
    }
}

static int next_script_byte(const VM *vm, KeyboardState *keyboard) {
    if (keyboard->ended || vm->instruction_count < keyboard->events[keyboard->next_event].at) {
        return -1;
    }
    const KeyEvent *event = &keyboard->events[keyboard->next_event];
    int byte = keyboard->script[event->start + keyboard->event_offset++];
    if (keyboard->event_offset == event->length) {
        keyboard->event_offset = 0;
        keyboard->ended = ++keyboard->next_event == keyboard->event_count;
    }
    return byte;
}

// Returns the next byte of input if one is waiting, or -1
static int next_byte(const VM *vm, KeyboardState *keyboard) {
    struct pollfd input = { .fd = STDIN_FILENO, .events = POLLIN };
    unsigned char byte;

    if (keyboard->events) {
        return next_script_byte(vm, keyboard);
    }
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

// The key of an escape sequence by its final byte and the number before it, or 0 for one that stands for
// no key here
static int escape_key(int final, int number) {
    static const int numbered[] = { 0, KEY_HOME, KEY_INSERT, KEY_DELETE, KEY_END, KEY_PAGE_UP, KEY_PAGE_DOWN,
                                    KEY_HOME, KEY_END };
    if (final >= 'A' && final <= 'D') {
        return KEY_UP + final - 'A';
    }
    if (final == 'H' || final == 'F') {
        return final == 'H' ? KEY_HOME : KEY_END;
    }
    return final == '~' && number >= 1 && number <= 8 ? numbered[number] : 0;
}

// Moves waiting input into the queue; a terminal sends a whole escape sequence at once, so an
// escape that nothing follows yet, or another escape, is the Escape key
static void keyboard_poll(const VM *vm, KeyboardState *keyboard) {
    int byte;
    while (keyboard->count < KEYBOARD_QUEUE_SIZE && (byte = next_byte(vm, keyboard)) >= 0) {
        if (byte != KEY_ESCAPE) {
            add_key(keyboard, byte);
            continue;
        }
        int kind = next_byte(vm, keyboard);
        while (kind == KEY_ESCAPE) {
            add_key(keyboard, KEY_ESCAPE);
            kind = next_byte(vm, keyboard);
        }
        if (kind != '[' && kind != 'O') {
            add_key(keyboard, KEY_ESCAPE);
            if (kind >= 0) {
                add_key(keyboard, kind);
            }
            continue;
        }
        int final = next_byte(vm, keyboard), number = 0;
        while (final >= 0x20 && final < 0x40) {
            if (final >= '0' && final <= '9' && number < 100) {
                number = number * 10 + final - '0';
            }
            final = next_byte(vm, keyboard);
        }
        int key = escape_key(final, number);
        if (key) {
            add_key(keyboard, key);
        }
    }
}

// Port 0 reads the status bits, port 1 the next key (0 if none waits) and port 2 holds the
// interrupt vector that is requested while keys wait (0 for none)
static uint32_t keyboard_read(VM *vm, IODevice *device, uint16_t offset) {
    KeyboardState *keyboard = device->state;
    keyboard_claim(keyboard);
    if (keyboard->count == 0 && offset < 2) {
        keyboard_poll(vm, keyboard);
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

// Keys that arrive request the interrupt, and keys left waiting request it again with every poll.
// Scripted keys are looked for after every instruction, so they arrive at their exact count.
static void keyboard_tick(VM *vm, IODevice *device) {
    KeyboardState *keyboard = device->state;
    int waiting = keyboard->count, poll_due = --keyboard->countdown == 0;

    if (!poll_due && !keyboard->events) {
        return;
    }
    if (poll_due) {
        keyboard->countdown = KEYBOARD_POLL_INTERVAL;
    }
    keyboard_poll(vm, keyboard);
    if (keyboard->count > waiting || (poll_due && keyboard->count)) {
        cpu_request_interrupt(vm, keyboard->vector);
    } else if (keyboard->ended && !keyboard->count) {
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
    free(keyboard->events);
    free(keyboard->script);
    keyboard->events = NULL;
    keyboard->script = NULL;
}

// Decodes the keys of a script line in place; returns their number, or -1 for a bad escape
static int decode_keys(char *text) {
    static const char escapes[] = "n\nr\rt\te\033\\\\";
    char *out = text;
    for (const char *in = text; *in; in++) {
        const char *escape;
        if (*in != '\\') {
            *out++ = *in;
        } else if (in[1] == 'x' && isxdigit((unsigned char)in[2]) && isxdigit((unsigned char)in[3])) {
            char hex[3] = { in[2], in[3], 0 };
            *out++ = (char)strtol(hex, NULL, 16);
            in += 3;
        } else if (in[1] && (escape = strchr(escapes, in[1])) != NULL && (escape - escapes) % 2 == 0) {
            *out++ = escape[1];
            in++;
        } else {
            return -1;
        }
    }
    return (int)(out - text);
}

// Each line of a key script holds an instruction count, or +N for N instructions after the line
// before, a space and the keys, where \n, \r, \t, \e, \\ and \xNN stand for single bytes. Empty lines
// and lines that start with # are skipped.
int keyboard_script(VM *vm, IODevice *device, const char *path) {
    KeyboardState *keyboard = device->state;
    uint32_t size, at = 0, number = 0;
    const char *problem;
    char *text = (char *)read_binary_file(path, &size, &problem);

    if (!text) {
        return vm_raise(vm, VM_ERROR_IO_ERROR, "%s: %s", path, problem);
    }
    KeyEvent *events = calloc(size / 2 + 1, sizeof(KeyEvent));
    uint32_t count = 0;
    for (char *line = text, *next; events && line; line = next) {
        next = strchr(line, '\n');
        if (next) {
            *next++ = '\0';
        }
        number++;
        size_t length = strlen(line);
        if (length > 0 && line[length - 1] == '\r') {
            line[--length] = '\0';
        }
        if (length == 0 || line[0] == '#') {
            continue;
        }

        char *keys;
        unsigned long value = strtoul(line + (line[0] == '+'), &keys, 10);
        uint64_t when = line[0] == '+' ? (uint64_t)at + value : value;
        int decoded = *keys == ' ' ? decode_keys(keys + 1) : -1;
        if (!isdigit((unsigned char)line[line[0] == '+']) || when > UINT32_MAX || when < at || decoded <= 0) {
            free(events);
            free(text);
            return vm_raise(vm, VM_ERROR_IO_ERROR, "%s:%u: expected an instruction count at or after %u, "
                            "a space and keys", path, number, at);
        }
        at = (uint32_t)when;
        events[count++] = (KeyEvent){ at, (uint32_t)(keys + 1 - text), (uint32_t)decoded };
    }
    if (!events) {
        free(text);
        return vm_raise(vm, VM_ERROR_MEMORY_ALLOCATION, "Failed to allocate the key script");
    }

    keyboard_cleanup(vm, device);
    keyboard->events = events;
    keyboard->script = (uint8_t *)text;
    keyboard->event_count = count;
    keyboard->next_event = keyboard->event_offset = 0;
    keyboard->ended = count == 0;
    return VM_ERROR_NONE;
}

int keyboard_device(VM *vm, IODevice *device) {
    *device = (IODevice){ .name = "keyboard", .base_port = IO_PORT_KEYBOARD, .port_count = 3,
                          .read = keyboard_read, .write = keyboard_write, .tick = keyboard_tick,
                          .cleanup = keyboard_cleanup, .state = calloc(1, sizeof(KeyboardState)) };
    return device->state ? VM_ERROR_NONE : vm_raise(vm, VM_ERROR_MEMORY_ALLOCATION, "Failed to allocate the keyboard");
}
