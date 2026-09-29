#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asm.h"

// R0 and R5 to R15 are followed; BP, SP, PC and SR are left out
#define TRACKED     0xFFE1u
#define SP_REG      2
#define STACK_SLOTS 32
#define MAX_VISITS  64
#define MAX_ROUNDS  32

// Where the value in a register may come from inside a routine
enum { HELD_ENTRY = 1, HELD_RESULT = 2, HELD_ARGUMENT = 4, HELD_SCRATCH = 8 };

typedef struct {
    uint32_t address, size;
    Instruction instr;
    const SourceLine *line;
    int routine;            // the routine that starts here, or -1
    int target;             // the instruction a direct jump or call goes to, or -1
    int relocated;          // the linker supplies the target
} Insn;

// What calling a routine does to the registers. Its results are the registers it changes on
// every path, or changes after reading them; the others it changes are scratch.
typedef struct {
    size_t entry;
    uint16_t inputs, changes, results;
    uint32_t pops;          // argument bytes its RET removes
    int opaque;             // its effects are unknown
} Routine;

typedef struct {
    uint8_t held[16];
    uint8_t slot_held[STACK_SLOTS];
    int8_t slot_reg[STACK_SLOTS];
    int depth;
    int reached, visits;
} RoutineState;

// A value that nothing read yet, and one that a call or syscall overwrote before it was read
typedef struct {
    size_t set;             // instruction index + 1, 0 for none
    int written;            // an instruction wrote the value, rather than a call or syscall
    size_t lost, lost_by;
} Value;

typedef struct {
    Value regs[16];
    int reached, visits;
} Tracking;

typedef struct {
    size_t at, set, by;
    int reg;
} Warning;

typedef struct {
    Assembler *as;
    Insn *insns;
    size_t count;
    Routine *routines;
    size_t routine_count;
    Warning *warnings;
    size_t warning_count, warning_capacity;
} Checker;

static uint16_t bit(int reg) {
    return (uint16_t)(1u << (reg & 0x0F));
}

static uint16_t operand_uses(const Instruction *in, int reg) {
    return in->mode == REG_MODE || in->mode == REGM_MODE || in->mode == IDX_MODE ? bit(reg) : 0;
}

// The registers from first to last
static uint16_t register_range(int first, int last) {
    uint16_t range = 0;
    for (int r = first; r <= last; r++) {
        range |= bit(r);
    }
    return range;
}

// The registers an instruction other than a call or syscall reads and writes, SP included
static void effects(const Instruction *in, uint16_t *uses, uint16_t *defs) {
    const InstructionInfo *info = isa_by_opcode(in->opcode);
    uint16_t u = 0, d = 0;

    if (in->opcode == PUSHM_OP || in->opcode == POPM_OP) {
        *uses = in->opcode == PUSHM_OP ? register_range(in->reg1, in->reg2) : 0;
        *defs = in->opcode == POPM_OP ? register_range(in->reg1, in->reg2) & TRACKED : 0;
        return;
    }

    switch (info ? info->format : FMT_NONE) {
        case FMT_REG_OPERAND:
            u = operand_uses(in, in->reg2);
            switch (in->opcode) {
                case LOAD_OP: case LOADB_OP: case LOADW_OP: case LEA_OP: case ITOF_OP: case FTOI_OP:
                case FSQRT_OP: case POPCNT_OP: case CLZ_OP: case CTZ_OP: case IN_OP: case ALLOC_OP:
                    d = bit(in->reg1);
                    break;
                case STORE_OP: case STOREB_OP: case STOREW_OP: case CMP_OP: case TEST_OP: case FCMP_OP:
                case PROTECT_OP:
                    u |= bit(in->reg1);
                    break;
                default:
                    u |= bit(in->reg1);
                    d = bit(in->reg1);
                    break;
            }
            break;
        case FMT_REG:
            if (in->opcode == POP_OP) {
                d = bit(in->reg1);
            } else if (in->opcode == FREE_OP) {
                u = bit(in->reg1);
            } else {
                u = d = bit(in->reg1);
            }
            break;
        case FMT_REG_REG:
            u = bit(in->reg2);
            d = bit(in->reg1);
            break;
        case FMT_REG_REG_SIZE:
            u = bit(in->reg1) | bit(in->reg2) | (in->mode == REG_MODE ? bit((int)in->immediate) : 0);
            break;
        case FMT_REG_CTRL:
        case FMT_REG_COND:
            d = bit(in->reg1);
            break;
        case FMT_CTRL_REG:
            u = bit(in->reg1);
            break;
        case FMT_OPERAND:
            u = operand_uses(in, in->reg1);
            break;
        case FMT_OPERAND_REG:
            u = bit(in->reg1) | operand_uses(in, in->reg2);
            break;
        default:
            if (in->opcode == CPUID_OP) {
                u = bit(0);
                d = bit(0) | bit(5) | bit(6) | bit(7);
            } else if (in->opcode == POPA_OP) {
                d = TRACKED;
            }
            break;
    }
    *uses = u;
    *defs = d;
}

// Syscalls read their arguments from R0, R5 and R6, and all of them write a status to R5
static void syscall_registers(uint32_t number, uint16_t *inputs, uint16_t *results) {
    static const struct {
        uint8_t number;
        uint16_t inputs, results;
    } table[] = {
        { 0, 0x01, 0x20 }, { 1, 0x01, 0x20 }, { 2, 0x01, 0x20 }, { 3, 0x00, 0x21 }, { 4, 0x21, 0x21 },
        { 5, 0x01, 0x20 }, { 6, 0x21, 0x20 }, { 7, 0x01, 0x20 }, { 8, 0x00, 0x20 }, { 9, 0x01, 0x20 },
        { 10, 0x21, 0x21 }, { 11, 0x01, 0x21 }, { 12, 0x61, 0x21 }, { 13, 0x61, 0x21 }, { 14, 0x61, 0x21 },
        { 20, 0x01, 0x21 }, { 21, 0x01, 0x21 }, { 22, 0x61, 0x21 }, { 23, 0x00, 0xE1 }, { 30, 0x01, 0x00 },
        { 31, 0x01, 0x20 }, { 32, 0x00, 0x21 }, { 33, 0x00, 0x21 }, { 34, 0x61, 0x21 }, { 40, 0x01, 0x21 },
        { 41, 0x01, 0x21 },
    };
    *inputs = 0x61;
    *results = 0x21;
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (table[i].number == number) {
            *inputs = table[i].inputs;
            *results = table[i].results;
        }
    }
}

static int find_insn(const Checker *c, uint32_t address) {
    size_t low = 0, high = c->count;
    while (low < high) {
        size_t middle = (low + high) / 2;
        if (c->insns[middle].address < address) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low < c->count && c->insns[low].address == address ? (int)low : -1;
}

static int compare_insns(const void *a, const void *b) {
    const Insn *x = a, *y = b;
    return x->address < y->address ? -1 : x->address > y->address;
}

// The instruction that execution falls through to, or -1
static int next_insn(const Checker *c, size_t i) {
    return i + 1 < c->count && c->insns[i + 1].address == c->insns[i].address + c->insns[i].size ? (int)i + 1 : -1;
}

static int is_direct(const Instruction *in) {
    return in->mode == IMM_MODE;
}

static int load_program(Checker *c) {
    Assembler *as = c->as;
    const Section *text = &as->sections[SECTION_TEXT];

    c->insns = calloc(as->line_count ? as->line_count : 1, sizeof(Insn));
    if (!c->insns) {
        return 0;
    }
    for (size_t i = 0; i < as->line_count; i++) {
        const LineResult *r = &as->results[i];
        if (r->section != SECTION_TEXT || !r->is_code || r->size < 4 || r->address < text->base ||
            r->address - text->base + r->size > text->end - text->base) {
            continue;
        }
        const uint8_t *bytes = text->bytes + (r->address - text->base);
        uint32_t word = (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
                        (uint32_t)bytes[3] << 24;
        uint32_t extension = 0;
        if (r->size == 8) {
            extension = (uint32_t)bytes[4] | (uint32_t)bytes[5] << 8 | (uint32_t)bytes[6] << 16 |
                        (uint32_t)bytes[7] << 24;
        }
        Insn *insn = &c->insns[c->count++];
        insn->address = r->address;
        insn->size = r->size;
        insn->line = &as->lines[i];
        insn->relocated = r->relocated;
        isa_decode(word, extension, &insn->instr);
    }
    qsort(c->insns, c->count, sizeof(Insn), compare_insns);

    // Direct calls start routines
    c->routines = calloc(c->count ? c->count : 1, sizeof(Routine));
    if (!c->routines) {
        return 0;
    }
    for (size_t i = 0; i < c->count; i++) {
        c->insns[i].routine = -1;
    }
    for (size_t i = 0; i < c->count; i++) {
        Insn *insn = &c->insns[i];
        insn->target = -1;
        if (isa_has_relative_target(insn->instr.opcode) && is_direct(&insn->instr) && !insn->relocated) {
            insn->target = find_insn(c, insn->address + insn->size + insn->instr.immediate);
        }
        if (insn->instr.opcode == CALL_OP && insn->target >= 0 && c->insns[insn->target].routine < 0) {
            c->insns[insn->target].routine = (int)c->routine_count;
            c->routines[c->routine_count++].entry = (size_t)insn->target;
        }
    }
    return 1;
}

// ----- Routine summaries -----

static int push_slot(RoutineState *s, uint8_t held, int reg) {
    if (s->depth >= STACK_SLOTS) {
        return 0;
    }
    s->slot_held[s->depth] = held;
    s->slot_reg[s->depth++] = (int8_t)reg;
    return 1;
}

static int pop_slots(RoutineState *s, uint32_t count) {
    if ((uint32_t)s->depth < count) {
        return 0;
    }
    s->depth -= (int)count;
    return 1;
}

static void note_inputs(const RoutineState *s, uint16_t regs, uint16_t *inputs) {
    for (int r = 0; r < 16; r++) {
        if ((regs & TRACKED & bit(r)) && (s->held[r] & HELD_ENTRY)) {
            *inputs |= bit(r);
        }
    }
}

// Values passed to a nested call or syscall count as its arguments from then on
static void pass_arguments(RoutineState *s, uint16_t regs, uint16_t *inputs) {
    note_inputs(s, regs, inputs);
    for (int r = 0; r < 16; r++) {
        if ((regs & TRACKED & bit(r)) && (s->held[r] & HELD_RESULT)) {
            s->held[r] = (uint8_t)((s->held[r] & ~HELD_RESULT) | HELD_ARGUMENT);
        }
    }
}

static int apply_call(RoutineState *s, const Routine *callee, uint16_t *inputs) {
    pass_arguments(s, callee->inputs, inputs);
    for (int r = 0; r < 16; r++) {
        if (callee->changes & bit(r)) {
            s->held[r] = (callee->results & bit(r)) ? HELD_RESULT : HELD_SCRATCH;
        }
    }
    return pop_slots(s, callee->pops / 4);
}

// Joins a state into the one recorded for an instruction; returns 1 when that one changed and -1
// when the stacks disagree
static int join_state(RoutineState *into, const RoutineState *from) {
    if (!into->reached) {
        *into = *from;
        into->reached = 1;
        into->visits = 0;
        return 1;
    }
    if (into->depth != from->depth) {
        return -1;
    }
    int changed = 0;
    for (int r = 0; r < 16; r++) {
        uint8_t held = into->held[r] | from->held[r];
        changed |= held != into->held[r];
        into->held[r] = held;
    }
    for (int i = 0; i < into->depth; i++) {
        uint8_t held = into->slot_held[i] | from->slot_held[i];
        changed |= held != into->slot_held[i];
        into->slot_held[i] = held;
        if (into->slot_reg[i] != from->slot_reg[i] && into->slot_reg[i] != -1) {
            into->slot_reg[i] = -1;
            changed = 1;
        }
    }
    return changed;
}

typedef struct {
    RoutineState *states;
    size_t *work;
    size_t work_count;
    uint16_t inputs;
    uint8_t exits[16];
    int exited;
    int64_t pops;
    int opaque;
} RoutineWalk;

static void flow_to(Checker *c, RoutineWalk *w, int target, const RoutineState *s) {
    (void)c;
    if (target < 0) {
        return;
    }
    int changed = join_state(&w->states[target], s);
    if (changed < 0) {
        w->opaque = 1;
    } else if (changed && w->states[target].visits++ < MAX_VISITS) {
        w->work[w->work_count++] = (size_t)target;
    }
}

static void leave_routine(RoutineWalk *w, const RoutineState *s, uint32_t pops) {
    if (s->depth != 0 || (w->pops >= 0 && w->pops != pops)) {
        w->opaque = 1;
        return;
    }
    w->pops = pops;
    w->exited = 1;
    for (int r = 0; r < 16; r++) {
        w->exits[r] |= s->held[r];
    }
}

// Runs a call to another routine as the last thing this one does
static void tail_call(Checker *c, RoutineWalk *w, RoutineState *s, const Routine *callee) {
    (void)c;
    if (callee->opaque || !apply_call(s, callee, &w->inputs)) {
        w->opaque = 1;
        return;
    }
    leave_routine(w, s, callee->pops);
}

static void step_routine(Checker *c, RoutineWalk *w, size_t self, size_t i) {
    RoutineState s = w->states[i];
    const Insn *insn = &c->insns[i];
    const Instruction *in = &insn->instr;
    int next = next_insn(c, i);
    uint16_t uses, defs;

    switch (in->opcode) {
        case CALL_OP:
            if (insn->target < 0 || c->insns[insn->target].routine < 0) {
                w->opaque = 1;
                return;
            }
            if (c->routines[c->insns[insn->target].routine].opaque ||
                !apply_call(&s, &c->routines[c->insns[insn->target].routine], &w->inputs)) {
                w->opaque = 1;
                return;
            }
            break;
        case SYSCALL_OP: {
            uint16_t inputs, results;
            syscall_registers(in->immediate, &inputs, &results);
            pass_arguments(&s, inputs, &w->inputs);
            if (in->immediate == 30) {
                return;
            }
            for (int r = 0; r < 16; r++) {
                if (results & bit(r)) {
                    s.held[r] = HELD_RESULT;
                }
            }
            break;
        }
        case INT_OP:
            pass_arguments(&s, TRACKED, &w->inputs);
            break;
        case RET_OP:
            leave_routine(w, &s, in->immediate);
            return;
        case IRET_OP:
        case HALT_OP:
        case RESET_OP:
            w->opaque |= in->opcode == IRET_OP;
            return;
        case PUSH_OP:
            if (!push_slot(&s, in->mode == REG_MODE ? s.held[in->reg1 & 0x0F] : HELD_RESULT,
                           in->mode == REG_MODE ? in->reg1 : -1)) {
                w->opaque = 1;
                return;
            }
            note_inputs(&s, operand_uses(in, in->reg1) & (uint16_t)~(in->mode == REG_MODE ? bit(in->reg1) : 0),
                        &w->inputs);
            break;
        case POP_OP: {
            int reg = in->reg1 & 0x0F;
            if (s.depth == 0) {
                w->opaque = 1;
                return;
            }
            s.depth--;
            s.held[reg] = s.slot_reg[s.depth] == reg ? s.slot_held[s.depth] : HELD_RESULT;
            break;
        }
        case PUSHF_OP:
        case ENTER_OP:
            if (!push_slot(&s, HELD_RESULT, -1)) {
                w->opaque = 1;
                return;
            }
            break;
        case POPF_OP:
        case LEAVE_OP:
            if (!pop_slots(&s, 1)) {
                w->opaque = 1;
                return;
            }
            break;
        case PUSHA_OP:
        case PUSHM_OP:
            for (int r = in->opcode == PUSHA_OP ? 15 : in->reg2; r >= (in->opcode == PUSHA_OP ? 0 : in->reg1); r--) {
                if (!push_slot(&s, s.held[r], r)) {
                    w->opaque = 1;
                    return;
                }
            }
            break;
        case POPA_OP:
        case POPM_OP:
            for (int r = in->opcode == POPA_OP ? 0 : in->reg1; r <= (in->opcode == POPA_OP ? 15 : in->reg2); r++) {
                if (s.depth == 0) {
                    w->opaque = 1;
                    return;
                }
                s.depth--;
                s.held[r] = s.slot_reg[s.depth] == r ? s.slot_held[s.depth] : HELD_RESULT;
            }
            break;
        default:
            effects(in, &uses, &defs);
            note_inputs(&s, uses, &w->inputs);
            if (defs & bit(SP_REG)) {
                // Only constant adjustments of SP keep the stack model
                int32_t amount = (int32_t)in->immediate;
                if (in->mode != IMM_MODE || in->reg1 != SP_REG || amount % 4 != 0 ||
                    (in->opcode != ADD_OP && in->opcode != SUB_OP)) {
                    w->opaque = 1;
                    return;
                }
                int32_t slots = (in->opcode == ADD_OP ? -amount : amount) / 4;
                for (int32_t k = 0; k < slots; k++) {
                    if (!push_slot(&s, HELD_RESULT, -1)) {
                        w->opaque = 1;
                        return;
                    }
                }
                if (slots < 0 && !pop_slots(&s, (uint32_t)-slots)) {
                    w->opaque = 1;
                    return;
                }
            }
            for (int r = 0; r < 16; r++) {
                if (defs & TRACKED & bit(r)) {
                    s.held[r] = HELD_RESULT;
                }
            }
            break;
    }

    // Where execution goes next; entering another routine by a jump or by falling into it ends this one
    int jump = in->opcode == JMP_OP || isa_is_conditional_jump(in->opcode) || in->opcode == LOOP_OP;
    if (jump && !is_direct(in)) {
        w->opaque = 1;
        return;
    }
    int targets[2] = { jump ? insn->target : -1, in->opcode == JMP_OP ? -1 : next };
    if (jump && insn->target < 0) {
        w->opaque = 1;
        return;
    }
    for (int k = 0; k < 2; k++) {
        int t = targets[k];
        if (t < 0) {
            continue;
        }
        int routine = c->insns[t].routine;
        if (routine >= 0 && (size_t)t != c->routines[self].entry) {
            RoutineState copy = s;
            tail_call(c, w, &copy, &c->routines[routine]);
        } else {
            flow_to(c, w, t, &s);
        }
    }
}

// Returns 1 when the summary changed
static int summarize(Checker *c, size_t index, RoutineState *states, size_t *work) {
    Routine *routine = &c->routines[index];
    RoutineWalk w = { .states = states, .work = work, .pops = -1 };
    RoutineState start = { 0 };

    memset(states, 0, c->count * sizeof(RoutineState));
    for (int r = 0; r < 16; r++) {
        start.held[r] = HELD_ENTRY;
    }
    flow_to(c, &w, (int)routine->entry, &start);
    while (w.work_count > 0 && !w.opaque) {
        step_routine(c, &w, index, w.work[--w.work_count]);
    }

    Routine next = *routine;
    next.opaque = w.opaque;
    if (w.opaque) {
        next.inputs = TRACKED;
        next.changes = TRACKED;
        next.results = 0;
        next.pops = 0;
    } else {
        next.inputs = w.inputs & TRACKED;
        next.changes = next.results = 0;
        next.pops = w.pops > 0 ? (uint32_t)w.pops : 0;
        for (int r = 0; r < 16; r++) {
            if (!(TRACKED & bit(r)) || !(w.exits[r] & ~HELD_ENTRY)) {
                continue;
            }
            next.changes |= bit(r);
            if ((next.inputs & bit(r)) || w.exits[r] == HELD_RESULT) {
                next.results |= bit(r);
            }
        }
    }
    int changed = memcmp(&next, routine, sizeof(next)) != 0;
    *routine = next;
    return changed;
}

// ----- Warnings -----

static void add_warning(Checker *c, int reg, size_t set, size_t by, size_t at) {
    for (size_t i = 0; i < c->warning_count; i++) {
        if (c->warnings[i].at == at && c->warnings[i].reg == reg) {
            return;
        }
    }
    if (c->warning_count == c->warning_capacity) {
        size_t capacity = c->warning_capacity ? c->warning_capacity * 2 : 16;
        Warning *grown = realloc(c->warnings, capacity * sizeof(Warning));
        if (!grown) {
            return;
        }
        c->warnings = grown;
        c->warning_capacity = capacity;
    }
    c->warnings[c->warning_count++] = (Warning){ at, set, by, reg };
}

static void read_values(Checker *c, Tracking *t, uint16_t regs, size_t at) {
    for (int r = 0; r < 16; r++) {
        if (!(regs & TRACKED & bit(r))) {
            continue;
        }
        if (t->regs[r].lost_by) {
            add_warning(c, r, t->regs[r].lost - 1, t->regs[r].lost_by - 1, at);
            t->regs[r].lost = t->regs[r].lost_by = 0;
        }
        t->regs[r].set = 0;
    }
}

static void write_values(Tracking *t, uint16_t regs, size_t at, int written) {
    for (int r = 0; r < 16; r++) {
        if (regs & TRACKED & bit(r)) {
            t->regs[r] = (Value){ at + 1, written, 0, 0 };
        }
    }
}

// A value that nothing has read yet is lost when a call or syscall overwrites it; only_written
// limits that to values an instruction wrote
static void overwrite_values(Tracking *t, uint16_t regs, size_t at, int only_written) {
    for (int r = 0; r < 16; r++) {
        if ((regs & TRACKED & bit(r)) && t->regs[r].set && (t->regs[r].written || !only_written)) {
            t->regs[r].lost = t->regs[r].set;
            t->regs[r].lost_by = at + 1;
            t->regs[r].set = 0;
        }
    }
}

// Results of a call or syscall; a value that the program wrote and nothing read is lost to them
static void take_results(Tracking *t, uint16_t regs, uint16_t inputs, size_t at) {
    overwrite_values(t, regs & (uint16_t)~inputs, at, 1);
    for (int r = 0; r < 16; r++) {
        if (regs & TRACKED & bit(r)) {
            t->regs[r].set = at + 1;
            t->regs[r].written = 0;
        }
    }
}

static void forget_values(Tracking *t) {
    memset(t->regs, 0, sizeof(t->regs));
}

static size_t earliest(size_t a, size_t b) {
    return !a ? b : !b ? a : a < b ? a : b;
}

static int join_tracking(Tracking *into, const Tracking *from) {
    if (!into->reached) {
        *into = *from;
        into->reached = 1;
        into->visits = 0;
        return 1;
    }
    int changed = 0;
    for (int r = 0; r < 16; r++) {
        Value *v = &into->regs[r];
        const Value *f = &from->regs[r];
        size_t set = earliest(v->set, f->set);
        if (set != v->set) {
            v->set = set;
            v->written = set == f->set ? f->written : v->written;
            changed = 1;
        }
        if (f->lost && (!v->lost || f->lost < v->lost)) {
            v->lost = f->lost;
            v->lost_by = f->lost_by;
            changed = 1;
        }
    }
    return changed;
}

// Follows the values in the registers from one starting point until returns and jumps elsewhere
static void check_from(Checker *c, size_t start, Tracking *states, size_t *work) {
    size_t work_count = 0;
    Tracking empty = { 0 };

    memset(states, 0, c->count * sizeof(Tracking));
    join_tracking(&states[start], &empty);
    work[work_count++] = start;

    while (work_count > 0) {
        size_t i = work[--work_count];
        Tracking t = states[i];
        const Insn *insn = &c->insns[i];
        const Instruction *in = &insn->instr;
        uint16_t uses, defs;
        int stop = 0;

        if (in->opcode == CALL_OP) {
            const Routine *callee = insn->target >= 0 && c->insns[insn->target].routine >= 0
                                        ? &c->routines[c->insns[insn->target].routine] : NULL;
            if (!callee || callee->opaque) {
                forget_values(&t);
            } else {
                read_values(c, &t, callee->inputs, i);
                take_results(&t, callee->results, callee->inputs, i);
                overwrite_values(&t, callee->changes & (uint16_t)~(callee->results | callee->inputs), i, 0);
            }
        } else if (in->opcode == SYSCALL_OP) {
            uint16_t inputs, results;
            syscall_registers(in->immediate, &inputs, &results);
            read_values(c, &t, inputs, i);
            take_results(&t, results, inputs, i);
            stop = in->immediate == 30;
        } else if (in->opcode == INT_OP) {
            forget_values(&t);
        } else if (in->opcode != PUSHA_OP && in->opcode != PUSHM_OP) {
            effects(in, &uses, &defs);
            read_values(c, &t, uses, i);
            write_values(&t, defs, i, 1);
        }

        int op = in->opcode;
        int jump = op == JMP_OP || isa_is_conditional_jump((uint8_t)op) || op == LOOP_OP;
        if (stop || op == RET_OP || op == IRET_OP || op == HALT_OP || op == RESET_OP || (jump && !is_direct(in))) {
            continue;
        }
        int next = next_insn(c, i);
        int targets[2] = { jump ? insn->target : -1, op == JMP_OP ? -1 : next };
        for (int k = 0; k < 2; k++) {
            int target = targets[k];
            if (target < 0) {
                continue;
            }
            // Entering another routine other than by a call ends the path there, after its inputs are read
            int routine = c->insns[target].routine;
            if (routine >= 0 && (size_t)target != start) {
                Tracking copy = t;
                if (!c->routines[routine].opaque) {
                    read_values(c, &copy, c->routines[routine].inputs, i);
                }
                continue;
            }
            if (join_tracking(&states[target], &t) && states[target].visits++ < MAX_VISITS) {
                work[work_count++] = (size_t)target;
            }
        }
    }
}

// Names the call or syscall that overwrote a value, as in "CALL name" or "SYSCALL #2"
static void describe_overwrite(const Checker *c, const Insn *insn, char *out, size_t size) {
    if (insn->instr.opcode == SYSCALL_OP) {
        snprintf(out, size, "SYSCALL #%u", insn->instr.immediate);
        return;
    }
    const SymbolTable *symbols = &c->as->symbols;
    uint32_t target = insn->target >= 0 ? c->insns[insn->target].address : 0;
    for (size_t i = 0; i < symbols->count; i++) {
        const AsmSymbol *sym = &symbols->items[i];
        if (insn->target >= 0 && sym->defined && sym->kind == SYM_CODE && (uint32_t)sym->value == target) {
            snprintf(out, size, "CALL %s", sym->name);
            return;
        }
    }
    snprintf(out, size, "CALL 0x%04X", target);
}

static void describe_line(const SourceLine *line, const SourceLine *here, char *out, size_t size) {
    if (strcmp(line->file, here->file) == 0) {
        snprintf(out, size, "line %d", line->number);
    } else {
        snprintf(out, size, "%s:%d", line->file, line->number);
    }
}

static int compare_warnings(const void *a, const void *b) {
    const Warning *x = a, *y = b;
    if (x->at != y->at) {
        return x->at < y->at ? -1 : 1;
    }
    return x->reg - y->reg;
}

// Warns where a register is read after a call or syscall overwrote the value that was set in it
// before and never read; returns the number of warnings
int check_registers(Assembler *as) {
    Checker c = { .as = as };
    int reported = 0;

    if (!load_program(&c)) {
        free(c.insns);
        return 0;
    }
    size_t states_size = c.count ? c.count : 1;
    RoutineState *routine_states = calloc(states_size, sizeof(RoutineState));
    Tracking *tracking = calloc(states_size, sizeof(Tracking));
    size_t *work = calloc(states_size * (MAX_VISITS + 1) * 2, sizeof(size_t));
    if (!routine_states || !tracking || !work) {
        goto done;
    }

    for (int round = 0, changed = 1; changed && round < MAX_ROUNDS; round++) {
        changed = 0;
        for (size_t r = 0; r < c.routine_count; r++) {
            changed |= summarize(&c, r, routine_states, work);
        }
    }

    // Every routine, every code label and the entry point start a path, so code that is only
    // reached through tables is checked too
    for (size_t i = 0; i < c.count; i++) {
        int start = c.insns[i].routine >= 0 || c.insns[i].address == (uint32_t)as->entry;
        for (size_t s = 0; !start && s < as->symbols.count; s++) {
            const AsmSymbol *sym = &as->symbols.items[s];
            start = sym->defined && sym->kind == SYM_CODE && (uint32_t)sym->value == c.insns[i].address;
        }
        if (start) {
            check_from(&c, i, tracking, work);
        }
    }

    if (c.warning_count > 1) {
        qsort(c.warnings, c.warning_count, sizeof(Warning), compare_warnings);
    }
    for (size_t i = 0; i < c.warning_count; i++) {
        const Warning *warning = &c.warnings[i];
        const SourceLine *here = c.insns[warning->at].line;
        char set[160], by[160], call[160];
        describe_line(c.insns[warning->set].line, here, set, sizeof(set));
        describe_line(c.insns[warning->by].line, here, by, sizeof(by));
        describe_overwrite(&c, &c.insns[warning->by], call, sizeof(call));
        fprintf(stderr, "%s:%d: warning: %s set at %s is overwritten by %s at %s before it is read here\n",
                here->file, here->number, isa_register_name((uint8_t)warning->reg), set, call, by);
        reported++;
    }

done:
    free(routine_states);
    free(tracking);
    free(work);
    free(c.warnings);
    free(c.routines);
    free(c.insns);
    return reported;
}
