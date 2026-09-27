#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "disassembler.h"
#include "monitor.h"
#include "vm.h"

// Prints the address, its label and the instruction there the way a trace shows them, padding the
// instruction to width columns
static void print_instruction(const VM *vm, uint32_t pc, int width) {
    Instruction instr;
    char where[80], text[160] = "?";

    if (vm_peek_instruction(vm, pc, &instr)) {
        disasm_format(&instr, pc, vm->debug_info, text, sizeof(text));
    }
    debug_describe(vm->debug_info, pc, where, sizeof(where));
    fprintf(stderr, "0x%04X %-20s %-*s", pc, where, width, text);
}

static void record_history(const VM *vm, Monitor *monitor, const uint32_t *before) {
    HistoryEntry *entry = &monitor->history[monitor->history_count++ % monitor->history_size];
    entry->pc = vm->error_pc;
    entry->exception = vm->exception;
    entry->changed = 0;
    memcpy(entry->registers, vm->registers, sizeof(entry->registers));
    for (int i = 0; i < 16; i++) {
        if (i != R3_PC && before[i] != vm->registers[i]) {
            entry->changed |= (uint16_t)(1u << i);
        }
    }
}

// Runs like vm_run, optionally printing each instruction on stderr before executing it, counting
// how often each instruction of the loaded code executes and remembering the last instructions
int monitor_run(VM *vm, Monitor *monitor) {
    uint32_t before[16];

    while (!vm->halted) {
        if (monitor->trace) {
            fflush(stdout);
            print_instruction(vm, vm->registers[R3_PC], 0);
            fprintf(stderr, "\n");
        }
        if (monitor->history) {
            memcpy(before, vm->registers, sizeof(before));
        }

        int result = vm_step(vm);
        if (result != VM_ERROR_NONE) {
            return result;
        }
        if (monitor->trace && vm->exception) {
            fprintf(stderr, "vm: exception %u: %s\n", vm->exception, vm->exception_message);
        }
        if (monitor->counts && vm->error_pc < vm->code_end) {
            monitor->counts[vm->error_pc / 4]++;
        }
        if (monitor->history) {
            record_history(vm, monitor, before);
        }
    }
    return VM_ERROR_NONE;
}

// Prints the remembered instructions, oldest first, each with the registers it changed
void monitor_report_history(const VM *vm, const Monitor *monitor) {
    uint64_t shown = monitor->history_count < monitor->history_size ? monitor->history_count : monitor->history_size;
    if (!monitor->history || shown == 0) {
        return;
    }
    fprintf(stderr, "vm: the last %llu instructions:\n", (unsigned long long)shown);
    for (uint64_t n = monitor->history_count - shown; n < monitor->history_count; n++) {
        const HistoryEntry *entry = &monitor->history[n % monitor->history_size];
        print_instruction(vm, entry->pc, entry->changed || entry->exception ? 24 : 0);
        for (int i = 0; i < 16; i++) {
            if (entry->changed & (1u << i)) {
                fprintf(stderr, " %s=0x%08X", isa_register_name((uint8_t)i), entry->registers[i]);
            }
        }
        if (entry->exception) {
            fprintf(stderr, " exception %u", entry->exception);
        }
        fprintf(stderr, "\n");
    }
}

typedef struct {
    uint32_t address;
    uint64_t count;
} ProfileEntry;

static int compare_entries(const void *a, const void *b) {
    const ProfileEntry *x = a, *y = b;
    if (x->count != y->count) {
        return x->count < y->count ? 1 : -1;
    }
    return x->address < y->address ? -1 : x->address > y->address;
}

// Sums the counts under the closest preceding label, or per address without debug information
void monitor_report_profile(const VM *vm, const Monitor *monitor) {
    enum { SHOWN = 15 };
    uint32_t slots = (vm->code_end + 3) / 4;
    ProfileEntry *entries = calloc(slots ? slots : 1, sizeof(ProfileEntry));
    size_t used = 0;
    uint64_t total = 0;

    if (!entries) {
        return;
    }
    const uint32_t *counts = monitor->counts;
    for (uint32_t i = 0; i < slots; i++) {
        if (counts[i] == 0) {
            continue;
        }
        const Symbol *sym = debug_symbol_near(vm->debug_info, i * 4);
        uint32_t key = sym ? sym->address : i * 4;
        size_t e = 0;
        while (e < used && entries[e].address != key) {
            e++;
        }
        if (e == used) {
            entries[used++].address = key;
        }
        entries[e].count += counts[i];
        total += counts[i];
    }
    qsort(entries, used, sizeof(ProfileEntry), compare_entries);

    fprintf(stderr, "vm: profile of %llu instructions\n", (unsigned long long)total);
    fprintf(stderr, "%12s  %6s  %s\n", "count", "share", "location");
    for (size_t e = 0; e < used && e < SHOWN; e++) {
        char where[80];
        debug_describe(vm->debug_info, entries[e].address, where, sizeof(where));
        fprintf(stderr, "%12llu  %5.1f%%  0x%04X%s%s\n", (unsigned long long)entries[e].count,
                100.0 * (double)entries[e].count / (double)total, entries[e].address, where[0] ? " " : "", where);
    }
    if (used > SHOWN) {
        fprintf(stderr, "%12s  %6s  %zu more\n", "", "", used - SHOWN);
    }
    free(entries);
}
