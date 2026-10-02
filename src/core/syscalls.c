#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "binfmt.h"
#include "cpu.h"
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

// Stops the program with the message at address, reported as if it had happened levels calls further out
static void abort_program(VM *vm, uint32_t address, uint32_t levels) {
    char message[200];
    size_t length = 0;
    for (; length + 1 < sizeof(message); length++) {
        uint8_t c = memory_read_byte(vm, address + (uint32_t)length);
        if (vm->last_error != VM_ERROR_NONE) {
            return;
        }
        if (c == 0) {
            break;
        }
        message[length] = (char)c;
    }
    message[length] = '\0';
    uint32_t site = cpu_unwind_calls(vm, levels, vm->error_pc);
    vm_raise(vm, VM_ERROR_ABORT, "%s", message);
    vm->error_pc = site;
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

// Prints a single-precision float with up to six significant digits
static void print_float(uint32_t bits) {
    float value;
    memcpy(&value, &bits, sizeof(value));
    printf("%g", value);
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
    vm->fixed_clock = -1;
}

int64_t days_from_civil(int64_t year, unsigned month, unsigned day) {
    year -= month <= 2;
    int64_t era = (year >= 0 ? year : year - 399) / 400;
    unsigned year_of_era = (unsigned)(year - era * 400);
    unsigned day_of_year = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + (int64_t)day_of_era - 719468;
}

// The local date and time as seconds since 1970, counted as if the local time were UTC
static uint32_t local_clock(const VM *vm) {
    if (vm->fixed_clock >= 0) {
        return (uint32_t)vm->fixed_clock;
    }
    time_t now = time(NULL);
    struct tm local;
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    int64_t days = days_from_civil(local.tm_year + 1900, (unsigned)local.tm_mon + 1, (unsigned)local.tm_mday);
    return (uint32_t)(days * 86400 + local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec);
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
    if (memory_check_range(vm, address, count, writing ? PROT_READ : PROT_WRITE) != VM_ERROR_NONE) {
        return;
    }

    if (file != stdout) {
        fflush(stdout);
    }
    // Pages of the buffer need not be contiguous in physical memory, so the data goes through a chunk
    uint8_t chunk[4096];
    uint32_t moved = 0;
    while (moved < count) {
        uint32_t size = count - moved < sizeof(chunk) ? count - moved : (uint32_t)sizeof(chunk);
        size_t done;
        if (writing) {
            memory_read(vm, address + moved, chunk, size);
            done = fwrite(chunk, 1, size, file);
        } else {
            done = fread(chunk, 1, size, file);
            memory_write(vm, address + moved, chunk, (uint32_t)done);
        }
        moved += (uint32_t)done;
        if (done < size) {
            break;
        }
    }
    if (writing && file != stdout) {
        fflush(file);
    }
    vm->registers[R0_ACC] = moved;
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
    if (memory_check_range(vm, address, copied + 1, PROT_WRITE) == VM_ERROR_NONE) {
        memory_write(vm, address, arg, copied);
        memory_write_byte(vm, address + copied, 0);
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

// The bytes of a range the program may read: memory itself where nothing has to be translated, otherwise a
// copy in *copy for the caller to free; NULL after a fault
static const uint8_t *readable(VM *vm, uint32_t address, uint32_t size, uint8_t **copy) {
    static const uint8_t none[1];
    *copy = NULL;
    if (size == 0) {
        return none;
    }
    const uint8_t *bytes = memory_direct(vm, address, size, PROT_READ);
    if (bytes) {
        return bytes;
    }
    if (memory_check_range(vm, address, size, PROT_READ) != VM_ERROR_NONE) {
        return NULL;
    }
    *copy = malloc(size);
    if (!*copy) {
        vm_raise(vm, VM_ERROR_MEMORY_ALLOCATION, "Out of host memory reading %u bytes", size);
        return NULL;
    }
    memory_read(vm, address, *copy, size);
    return *copy;
}

// The difference of the first bytes that differ, so negative, zero or positive as the count bytes at a sort
// before, with or after those at b
static uint32_t compare_bytes(VM *vm, uint32_t a, uint32_t b, uint32_t count) {
    uint8_t *copy_a, *copy_b = NULL;
    const uint8_t *x = readable(vm, a, count, &copy_a);
    const uint8_t *y = x ? readable(vm, b, count, &copy_b) : NULL;
    int32_t result = 0;
    if (y) {
        uint32_t i = 0;
        while (count - i >= 64 && memcmp(x + i, y + i, 64) == 0) {
            i += 64;
        }
        while (i < count && x[i] == y[i]) {
            i++;
        }
        result = i < count ? x[i] - y[i] : 0;
    }
    free(copy_a);
    free(copy_b);
    return (uint32_t)result;
}

// The index of the first byte equal to value, or 0xFFFFFFFF
static uint32_t find_byte(VM *vm, uint32_t address, uint8_t value, uint32_t count) {
    uint8_t *copy;
    const uint8_t *bytes = readable(vm, address, count, &copy);
    if (!bytes) {
        return 0;
    }
    const uint8_t *at = count ? memchr(bytes, value, count) : NULL;
    uint32_t index = at ? (uint32_t)(at - bytes) : UINT32_MAX;
    free(copy);
    return index;
}

static uint32_t count_byte(VM *vm, uint32_t address, uint8_t value, uint32_t count) {
    uint8_t *copy;
    const uint8_t *bytes = readable(vm, address, count, &copy);
    uint32_t found = 0;
    if (bytes) {
        for (uint32_t i = 0; i < count; i++) {
            found += bytes[i] == value;
        }
    }
    free(copy);
    return found;
}

// FNV-1a over the bytes, starting from hash, so that a long text can be hashed in pieces
static uint32_t hash_bytes(VM *vm, uint32_t address, uint32_t hash, uint32_t count) {
    uint8_t *copy;
    const uint8_t *bytes = readable(vm, address, count, &copy);
    if (!bytes) {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    free(copy);
    return hash;
}

// Sorts a few numbers in place, where a radix sort would spend most of its time on its tables
static void insertion_sort(uint32_t *values, uint32_t count) {
    for (uint32_t i = 1; i < count; i++) {
        uint32_t value = values[i];
        uint32_t j = i;
        for (; j > 0 && values[j - 1] > value; j--) {
            values[j] = values[j - 1];
        }
        values[j] = value;
    }
}

// Sorts numbers by their bytes in four passes, skipping a pass where all of them share the byte, with spare
// as room for as many; returns the array that holds them sorted, which is values or spare
static uint32_t *radix_sort(uint32_t *values, uint32_t *spare, uint32_t count) {
    for (int shift = 0; shift < 32 && count > 0; shift += 8) {
        uint32_t starts[256] = { 0 };
        for (uint32_t i = 0; i < count; i++) {
            starts[values[i] >> shift & 0xFF]++;
        }
        if (starts[values[0] >> shift & 0xFF] == count) {
            continue;
        }
        for (uint32_t b = 0, at = 0; b < 256; b++) {
            uint32_t n = starts[b];
            starts[b] = at;
            at += n;
        }
        for (uint32_t i = 0; i < count; i++) {
            spare[starts[values[i] >> shift & 0xFF]++] = values[i];
        }
        uint32_t *sorted = spare;
        spare = values;
        values = sorted;
    }
    return values;
}

// Sorts count signed 32-bit numbers at address, smallest first
static uint32_t sort_numbers(VM *vm, uint32_t address, uint32_t count) {
    if (count > UINT32_MAX / 4) {
        vm_raise(vm, VM_ERROR_SEGMENTATION_FAULT, "Sorting %u numbers reaches past the end of memory", count);
        return 0;
    }
    uint32_t size = count * 4;
    if (memory_check_range(vm, address, size, PROT_WRITE) != VM_ERROR_NONE ||
        memory_check_range(vm, address, size, PROT_READ) != VM_ERROR_NONE) {
        return 0;
    }
    uint32_t *values = malloc(((size_t)count + 1) * 2 * sizeof(uint32_t));
    if (!values) {
        vm_raise(vm, VM_ERROR_MEMORY_ALLOCATION, "Out of host memory sorting %u numbers", count);
        return 0;
    }
    memory_read(vm, address, values, size);
    // Flipping the sign bit orders signed numbers as unsigned ones
    for (uint32_t i = 0; i < count; i++) {
        values[i] = read_le32((const uint8_t *)&values[i]) ^ 0x80000000u;
    }
    uint32_t *sorted = values;
    if (count < 48) {
        insertion_sort(values, count);
    } else {
        sorted = radix_sort(values, values + count + 1, count);
    }
    for (uint32_t i = 0; i < count; i++) {
        write_le32((uint8_t *)&sorted[i], sorted[i] ^ 0x80000000u);
    }
    memory_write(vm, address, sorted, size);
    free(values);
    return count;
}

int syscall_dispatch(VM *vm, uint32_t number) {
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
            fflush(stdout);
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
        case SYS_PRINT_FLOAT:
            print_float(arg0);
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
        case SYS_FILL:
            r[R0_ACC] = memory_set(vm, arg0, (uint8_t)arg1, arg2) == VM_ERROR_NONE ? arg2 : 0;
            syscall_status(vm);
            break;
        case SYS_COMPARE:
            r[R0_ACC] = compare_bytes(vm, arg0, arg1, arg2);
            syscall_status(vm);
            break;
        case SYS_FIND:
            r[R0_ACC] = find_byte(vm, arg0, (uint8_t)arg1, arg2);
            syscall_status(vm);
            break;
        case SYS_COUNT:
            r[R0_ACC] = count_byte(vm, arg0, (uint8_t)arg1, arg2);
            syscall_status(vm);
            break;
        case SYS_SORT:
            r[R0_ACC] = sort_numbers(vm, arg0, arg2);
            syscall_status(vm);
            break;
        case SYS_HASH:
            r[R0_ACC] = hash_bytes(vm, arg0, arg1, arg2);
            syscall_status(vm);
            break;

        case SYS_EXIT:
            vm->exit_code = arg0;
            vm->halted = 1;
            break;
        case SYS_SLEEP:
            fflush(stdout);
            if (!vm->skip_sleep) {
                sleep_ms(arg0);
            }
            break;
        case SYS_TIME:
            r[R0_ACC] = (uint32_t)(monotonic_ms() - vm->start_ms);
            break;
        case SYS_TICKS:
            r[R0_ACC] = vm->instruction_count;
            break;
        case SYS_CLOCK:
            r[R0_ACC] = local_clock(vm);
            break;
        case SYS_ARGUMENT:
            program_argument(vm, arg0, arg1, arg2);
            syscall_status(vm);
            break;

        case SYS_ABORT:
            abort_program(vm, arg0, arg1);
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
    return vm->last_error;
}
