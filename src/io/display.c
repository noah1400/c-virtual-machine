#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "devices.h"
#include "vm.h"

#define DISPLAY_COLUMNS 80
#define DISPLAY_ROWS    25
#define DISPLAY_CELLS   (DISPLAY_COLUMNS * DISPLAY_ROWS)

// Each cell is a character byte followed by an attribute byte, with the foreground color in the
// low nibble and the background in the high nibble, both from the 16 ANSI colors
typedef struct {
    uint32_t buffer;                    // physical address of the cells, 0 while the display is off
    uint8_t shown[DISPLAY_CELLS * 2];   // what the terminal shows
    int drawn;
    int text;                           // print frames as plain text instead of drawing
    uint32_t refreshes;
} DisplayState;

static void set_colors(uint8_t attribute) {
    int foreground = attribute & 0x0F, background = attribute >> 4;
    printf("\033[0;%d;%dm", foreground < 8 ? 30 + foreground : 82 + foreground,
           background < 8 ? 40 + background : 92 + background);
}

static char visible(uint8_t c) {
    return c >= 32 && c < 127 ? (char)c : ' ';
}

// Prints the characters of the display when they changed since the last frame, with trailing
// spaces left out
static void print_frame(VM *vm, DisplayState *display, const uint8_t *cells) {
    int changed = !display->drawn;
    for (int i = 0; i < DISPLAY_CELLS && !changed; i++) {
        changed = visible(cells[2 * i]) != visible(display->shown[2 * i]);
    }
    if (!changed) {
        return;
    }
    printf("--- refresh %u, instruction %u ---\n", display->refreshes, vm->instruction_count);
    for (int row = 0; row < DISPLAY_ROWS; row++) {
        char line[DISPLAY_COLUMNS];
        int length = 0;
        for (int column = 0; column < DISPLAY_COLUMNS; column++) {
            line[column] = visible(cells[2 * (row * DISPLAY_COLUMNS + column)]);
            if (line[column] != ' ') {
                length = column + 1;
            }
        }
        printf("%.*s\n", length, line);
    }
    fflush(stdout);
}

// Draws the cells that changed since the last refresh
static void display_refresh(VM *vm, DisplayState *display) {
    if (!display->buffer) {
        return;
    }
    if ((uint64_t)display->buffer + sizeof(display->shown) > vm->memory_size) {
        vm_raise(vm, VM_ERROR_IO_ERROR, "Display buffer 0x%08X lies outside memory", display->buffer);
        return;
    }

    const uint8_t *cells = vm->memory + display->buffer;
    display->refreshes++;
    if (display->text) {
        print_frame(vm, display, cells);
        memcpy(display->shown, cells, sizeof(display->shown));
        display->drawn = 1;
        return;
    }

    int attribute = -1, next = -1;
    for (int i = 0; i < DISPLAY_CELLS; i++) {
        const uint8_t *cell = cells + 2 * i;
        if (display->drawn && memcmp(cell, display->shown + 2 * i, 2) == 0) {
            continue;
        }
        if (i != next || i % DISPLAY_COLUMNS == 0) {
            printf("\033[%d;%dH", i / DISPLAY_COLUMNS + 1, i % DISPLAY_COLUMNS + 1);
        }
        if (cell[1] != attribute) {
            set_colors(cell[1]);
            attribute = cell[1];
        }
        putchar(visible(cell[0]));
        next = i + 1;
    }
    if (attribute >= 0) {
        printf("\033[0m");
    }
    memcpy(display->shown, cells, sizeof(display->shown));
    display->drawn = 1;
    fflush(stdout);
}

// Gives the terminal back with the cursor below the display
static void display_off(DisplayState *display) {
    if (display->buffer && !display->text) {
        printf("\033[0m\033[?25h\033[%d;1H", DISPLAY_ROWS + 1);
        fflush(stdout);
    }
    display->buffer = 0;
}

// Port 0 holds the buffer address; setting one clears the terminal and 0 turns the display off.
// Writing port 1 refreshes; ports 2 and 3 read the columns and rows.
static uint32_t display_read(VM *vm, IODevice *device, uint16_t offset) {
    DisplayState *display = device->state;
    (void)vm;
    switch (offset) {
        case 0:
            return display->buffer;
        case 2:
            return DISPLAY_COLUMNS;
        case 3:
            return DISPLAY_ROWS;
        default:
            return 0;
    }
}

static void display_write(VM *vm, IODevice *device, uint16_t offset, uint32_t value) {
    DisplayState *display = device->state;
    switch (offset) {
        case 0:
            if (value && !display->buffer && !display->text) {
                printf("\033[?25l\033[2J");
                fflush(stdout);
            } else if (!value) {
                display_off(display);
            }
            display->buffer = value;
            display->drawn = 0;
            break;
        case 1:
            display_refresh(vm, display);
            break;
        default:
            break;
    }
}

static void display_cleanup(VM *vm, IODevice *device) {
    (void)vm;
    display_off(device->state);
}

void display_as_text(IODevice *device) {
    DisplayState *display = device->state;
    display->text = 1;
}

int display_device(VM *vm, IODevice *device) {
    *device = (IODevice){ .name = "display", .base_port = IO_PORT_DISPLAY, .port_count = 4,
                          .read = display_read, .write = display_write, .cleanup = display_cleanup,
                          .state = calloc(1, sizeof(DisplayState)) };
    return device->state ? VM_ERROR_NONE : vm_raise(vm, VM_ERROR_MEMORY_ALLOCATION, "Failed to allocate the display");
}
