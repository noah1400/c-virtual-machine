#include <stdio.h>
#include <stdlib.h>
#include "debug.h"
#include "disassembler.h"
#include "monitor.h"
#include "vm.h"

// Runs like vm_run, optionally printing each instruction on stderr before executing it and
// counting how often each instruction of the loaded code executes
int monitor_run(VM *vm, Monitor *monitor) {
    while (!vm->halted) {
        if (monitor->trace) {
            uint32_t pc = vm->registers[R3_PC];
            Instruction instr;
            char where[80], text[160] = "?";

            if (vm_peek_instruction(vm, pc, &instr)) {
                disasm_format(&instr, pc, vm->debug_info, text, sizeof(text));
            }
            debug_describe(vm->debug_info, pc, where, sizeof(where));
            fflush(stdout);
            fprintf(stderr, "0x%04X %-20s %s\n", pc, where, text);
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
    }
    return VM_ERROR_NONE;
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
