#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "memory.h"
#include "syscalls.h"
#include "vm.h"

// Turns a fault raised while servicing a syscall into a status code in R5
static void syscall_status(VM *vm) {
    vm->registers[R5] = (uint32_t)vm->last_error;
    vm_clear_error(vm);
}

static void print_string(VM *vm, uint32_t address) {
    for (;;) {
        uint8_t c = memory_read_byte(vm, address++);
        if (c == 0 || vm->last_error != VM_ERROR_NONE) {
            break;
        }
        putchar(c);
    }
}

// Reads a line without its newline into a buffer of max_len bytes including the terminator
static void read_line(VM *vm, uint32_t address, uint32_t max_len) {
    uint32_t length = 0;
    int c = 0;

    if (max_len == 0) {
        vm->registers[R0_ACC] = 0;
        vm->registers[R5] = VM_ERROR_INVALID_ADDRESS;
        return;
    }

    while (length + 1 < max_len && (c = getchar()) != EOF && c != '\n') {
        memory_write_byte(vm, address + length++, (uint8_t)c);
    }
    memory_write_byte(vm, address + length, 0);

    vm->registers[R0_ACC] = length;
    if (c == EOF && length == 0) {
        vm->registers[R5] = VM_ERROR_IO_ERROR;
    }
}

static void print_in_base(uint32_t value, uint32_t base) {
    static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char buffer[33];
    int pos = 0;

    if (base < 2 || base > 36) {
        base = 10;
    }
    do {
        buffer[pos++] = digits[value % base];
        value /= base;
    } while (value > 0);

    while (pos > 0) {
        putchar(buffer[--pos]);
    }
}

// Prints a signed 16.16 fixed-point number with four decimals
static void print_fixed(uint32_t value) {
    int negative = (value & 0x80000000) != 0;
    uint32_t magnitude = negative ? 0u - value : value;
    uint32_t decimals = ((magnitude & 0xFFFF) * 10000u) >> 16;

    printf("%s%u.%04u", negative ? "-" : "", magnitude >> 16, decimals);
}

// Low byte selects the foreground (0-7, 0xFF resets), second byte the background (0-7)
static void set_color(uint32_t value) {
    uint8_t fg = value & 0xFF;
    uint8_t bg = (value >> 8) & 0xFF;

    if (fg == 0xFF) {
        printf("\033[0;39;49m");
    } else if (fg < 8 && bg < 8) {
        printf("\033[0;%d;%dm", 30 + fg, 40 + bg);
    } else if (fg < 8) {
        printf("\033[0;%dm", 30 + fg);
    }
}

static void sleep_ms(uint32_t ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

static uint64_t monotonic_ms(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
#endif
}

void syscalls_init(VM *vm) {
    vm->start_ms = monotonic_ms();
}

void syscalls_cleanup(VM *vm) {
    for (int i = 0; i < VM_MAX_FILES; i++) {
        if (vm->files[i]) {
            fclose(vm->files[i]);
            vm->files[i] = NULL;
        }
    }
}

static FILE *file_for_handle(VM *vm, uint32_t handle) {
    switch (handle) {
        case 0:
            return stdin;
        case 1:
            return stdout;
        case 2:
            return stderr;
        default:
            return handle - VM_FIRST_FILE_HANDLE < VM_MAX_FILES ? vm->files[handle - VM_FIRST_FILE_HANDLE] : NULL;
    }
}

// Copies a NUL-terminated string out of VM memory; returns 0 if it is unreadable or too long
static int read_string(VM *vm, uint32_t address, char *out, size_t size) {
    for (size_t i = 0; i < size; i++) {
        out[i] = (char)memory_read_byte(vm, address + (uint32_t)i);
        if (vm->last_error != VM_ERROR_NONE) {
            return 0;
        }
        if (out[i] == '\0') {
            return 1;
        }
    }
    return 0;
}

static void file_open(VM *vm, uint32_t path_address, uint32_t mode) {
    static const char *const modes[] = { "rb", "wb", "ab", "r+b", "w+b" };
    char path[256];
    int slot = 0;

    while (slot < VM_MAX_FILES && vm->files[slot]) {
        slot++;
    }
    if (!read_string(vm, path_address, path, sizeof(path)) || mode >= sizeof(modes) / sizeof(modes[0]) ||
        slot == VM_MAX_FILES || !(vm->files[slot] = fopen(path, modes[mode]))) {
        vm_raise(vm, VM_ERROR_IO_ERROR, "Cannot open file");
        vm->registers[R0_ACC] = 0;
        return;
    }
    vm->registers[R0_ACC] = VM_FIRST_FILE_HANDLE + (uint32_t)slot;
}

static void file_close(VM *vm, uint32_t handle) {
    FILE *file = handle >= VM_FIRST_FILE_HANDLE ? file_for_handle(vm, handle) : NULL;
    if (!file || fclose(file) != 0) {
        vm_raise(vm, VM_ERROR_IO_ERROR, "Invalid file handle");
    }
    if (file) {
        vm->files[handle - VM_FIRST_FILE_HANDLE] = NULL;
    }
}

// Transfers count bytes between a file and VM memory, returning the number moved in R0
static void file_transfer(VM *vm, uint32_t handle, uint32_t address, uint32_t count, int writing) {
    FILE *file = file_for_handle(vm, handle);

    vm->registers[R0_ACC] = 0;
    if (!file) {
        vm_raise(vm, VM_ERROR_IO_ERROR, "Invalid file handle");
        return;
    }
    if (memory_check_address_permissions(vm, address, count, writing ? PROT_READ : PROT_WRITE) != VM_ERROR_NONE) {
        return;
    }

    if (writing) {
        fflush(stdout);
    }
    size_t moved = writing ? fwrite(vm->memory + address, 1, count, file)
                           : fread(vm->memory + address, 1, count, file);
    if (writing) {
        fflush(file);
    }
    vm->registers[R0_ACC] = (uint32_t)moved;
    if (moved < count && ferror(file)) {
        clearerr(file);
        vm_raise(vm, VM_ERROR_IO_ERROR, "File transfer failed");
    }
}

static void file_seek(VM *vm, uint32_t handle, uint32_t offset, uint32_t whence) {
    static const int origins[] = { SEEK_SET, SEEK_CUR, SEEK_END };
    FILE *file = handle >= VM_FIRST_FILE_HANDLE ? file_for_handle(vm, handle) : NULL;
    long position;

    if (!file || whence > 2 || fseek(file, (int32_t)offset, origins[whence]) != 0 || (position = ftell(file)) < 0) {
        vm_raise(vm, VM_ERROR_IO_ERROR, "Cannot seek");
        vm->registers[R0_ACC] = 0;
        return;
    }
    vm->registers[R0_ACC] = (uint32_t)position;
}

// Copies argument index into a buffer of size bytes, truncating it to fit; R0 receives its full
// length, or 0xFFFFFFFF when there is no such argument
static void program_argument(VM *vm, uint32_t index, uint32_t address, uint32_t size) {
    if (index >= (uint32_t)vm->arg_count) {
        vm->registers[R0_ACC] = 0xFFFFFFFF;
        return;
    }

    const char *arg = vm->args[index];
    uint32_t length = (uint32_t)strlen(arg);
    vm->registers[R0_ACC] = length;
    if (size == 0) {
        return;
    }

    uint32_t copied = length < size - 1 ? length : size - 1;
    if (memory_check_address_permissions(vm, address, copied + 1, PROT_WRITE) == VM_ERROR_NONE) {
        memcpy(vm->memory + address, arg, copied);
        vm->memory[address + copied] = '\0';
    }
}

static uint32_t mix_seed(uint32_t seed) {
    seed += 0x9E3779B9;
    seed = (seed ^ (seed >> 16)) * 0x85EBCA6B;
    seed = (seed ^ (seed >> 13)) * 0xC2B2AE35;
    seed ^= seed >> 16;
    return seed ? seed : VM_RNG_DEFAULT_SEED;
}

// xorshift32 scaled to [0, max), or the full 32-bit range when max is 0
static uint32_t next_random(VM *vm, uint32_t max) {
    uint32_t x = vm->rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    vm->rng_state = x;
    return max ? (uint32_t)(((uint64_t)x * max) >> 32) : x;
}

int syscall_dispatch(VM *vm, uint16_t number) {
    uint32_t *r = vm->registers;
    uint32_t arg0 = r[R0_ACC];
    uint32_t arg1 = r[R5];
    uint32_t arg2 = r[R6];

    r[R5] = 0;

    switch (number) {
        case SYS_PUTCHAR:
            putchar((int)(arg0 & 0xFF));
            break;
        case SYS_PRINT_INT:
            printf("%d", (int32_t)arg0);
            break;
        case SYS_PRINT_STRING:
            print_string(vm, arg0);
            break;
        case SYS_GETCHAR: {
            int c = getchar();
            r[R0_ACC] = c == EOF ? 0 : (uint32_t)c;
            r[R5] = c == EOF ? VM_ERROR_IO_ERROR : 0;
            break;
        }
        case SYS_READ_LINE:
            fflush(stdout);
            read_line(vm, arg0, arg1);
            break;
        case SYS_PRINT_HEX:
            printf("0x%x", arg0);
            break;
        case SYS_PRINT_BASE:
            print_in_base(arg0, arg1);
            break;
        case SYS_PRINT_FIXED:
            print_fixed(arg0);
            break;
        case SYS_CLEAR_SCREEN:
            printf("\033[2J\033[H");
            break;
        case SYS_SET_COLOR:
            set_color(arg0);
            break;

        case SYS_OPEN:
            file_open(vm, arg0, arg1);
            syscall_status(vm);
            break;
        case SYS_CLOSE:
            file_close(vm, arg0);
            syscall_status(vm);
            break;
        case SYS_READ:
        case SYS_WRITE:
            file_transfer(vm, arg0, arg1, arg2, number == SYS_WRITE);
            syscall_status(vm);
            break;
        case SYS_SEEK:
            file_seek(vm, arg0, arg1, arg2);
            syscall_status(vm);
            break;

        case SYS_ALLOC:
            r[R0_ACC] = memory_allocate(vm, arg0);
            syscall_status(vm);
            break;
        case SYS_FREE:
            memory_free(vm, arg0);
            r[R0_ACC] = (uint32_t)vm->last_error;
            syscall_status(vm);
            break;
        case SYS_MEMCPY:
            r[R0_ACC] = memory_copy(vm, arg0, arg1, arg2) == VM_ERROR_NONE ? arg2 : 0;
            syscall_status(vm);
            break;
        case SYS_MEMINFO:
            r[R0_ACC] = vm->memory_size;
            memory_heap_stats(vm, &r[R6], &r[R7]);
            break;

        case SYS_EXIT:
            vm->exit_code = arg0;
            vm->halted = 1;
            break;
        case SYS_SLEEP:
            fflush(stdout);
            sleep_ms(arg0);
            break;
        case SYS_TIME:
            r[R0_ACC] = (uint32_t)(monotonic_ms() - vm->start_ms);
            break;
        case SYS_TICKS:
            r[R0_ACC] = vm->instruction_count;
            break;
        case SYS_ARGUMENT:
            program_argument(vm, arg0, arg1, arg2);
            syscall_status(vm);
            break;

        case SYS_RANDOM:
            r[R0_ACC] = next_random(vm, arg0);
            break;
        case SYS_SEED:
            vm->rng_state = mix_seed(arg0);
            r[R0_ACC] = 0;
            break;

        default:
            return vm_raise(vm, VM_ERROR_INVALID_SYSCALL, "Invalid system call: %u", number);
    }

    fflush(stdout);
    return vm->last_error;
}
