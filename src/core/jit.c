#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "binfmt.h"
#include "cpu.h"
#include "jit.h"
#include "memory.h"
#include "syscalls.h"
#include "vm.h"

// The JIT turns the instructions from a word that jumps go to often into x86-64 code, up to an instruction
// that always goes elsewhere. The code keeps SP in ebx and R0 in ebp, the other VM registers in memory and
// the flags as cpu_run keeps them, and checks the budget once per block. It hands an instruction that would fault, or that it does not
// compile, back to cpu_run, which runs it as before.

#if defined(__x86_64__) && defined(__GNUC__) && (defined(__linux__) || defined(__APPLE__))

#include <sys/mman.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#define CODE_SIZE (32u << 20)
#define BLOCK_LIMIT 256
#define STUB_LIMIT (4 * BLOCK_LIMIT)

// What the entry code gets from cpu_run and gives back to it
typedef struct {
    uint8_t *memory;
    VM *vm;
    void **entries;
    Flags flags;
    uint32_t left;
    uint32_t next;
    uint32_t how;
} Context;

typedef void (*Enter)(Context *context, void *code);

// A jump in compiled code to a word that had no compiled code yet
typedef struct {
    uint32_t word;
    uint32_t at;            // where its displacement lies in the code
    uint32_t next;          // 1 + the next link that waits for the same word, or 0
} Link;

struct Jit {
    uint8_t *code;          // executable memory
    size_t base;            // where blocks start, after the code that enters and leaves them
    size_t used;
    void **entries;         // for every word, the compiled code that starts there, or NULL
    uint8_t *kinds;         // for every word that compiled code starts at, its kind before
    uint32_t *starts;       // the words that compiled code starts at
    uint32_t start_count;
    uint32_t start_room;
    uint32_t words;
    uint32_t low;           // the bytes that compiled code came from
    uint32_t high;
    uint32_t flushes;       // how often all compiled code was dropped
    uint32_t *waiting;      // for every word, 1 + the first link that waits for compiled code there, or 0
    Link *links;
    uint32_t link_count;
    uint32_t link_room;
    Enter enter;
    uint8_t *exit;          // leaves compiled code with the word or address in eax and how in edx
};

// Host registers. Compiled code keeps SP in ebx, R0 in ebp, VM memory at r12, the VM in r13 and the budget
// in r14d.
enum { HAX, HCX, HDX, HBX, HSP, HBP, HSI, HDI, H8, H9, H10, H11, H12, H13, H14, H15 };

// x86 conditions
enum { CC_O = 0, CC_B = 2, CC_AE = 3, CC_E = 4, CC_NE = 5, CC_BE = 6, CC_A = 7, CC_S = 8, CC_NS = 9,
       CC_L = 12, CC_GE = 13, CC_LE = 14, CC_G = 15 };

// ALU operations in the order of their x86 encodings
enum { ADD = 0, OR = 1, AND = 4, SUB = 5, XOR = 6, CMP = 7 };

#define FLAGS(field) ((int32_t)(FLAGS_AT + offsetof(Flags, field)))
#define CONTROL(index) ((int32_t)(offsetof(VM, control) + 4 * (index)))
#define FIELD(field) ((int32_t)offsetof(VM, field))
#define REG(number) ((int32_t)(offsetof(VM, registers) + 4 * (number)))

// Compiled code keeps temporaries in the 8 bytes at [rsp]: slow paths at [rsp] and addresses it goes to after
// them at [rsp + GO_SLOT]. The context follows at [rsp + 8], the table of compiled entries at
// [rsp + ENTRIES_AT] and a copy of the flags at [rsp + FLAGS_AT].
#define GO_SLOT 4
#define ENTRIES_AT 16
#define FLAGS_AT 24
// with 8 bytes more than a multiple of 16, so that calls find the stack aligned
#define FRAME_SIZE ((FLAGS_AT + (int32_t)sizeof(Flags) + 7) / 16 * 16 + 8)

typedef struct {
    uint8_t *p;
    uint8_t *end;
} Code;

static void put(Code *c, uint32_t byte) {
    if (c->p < c->end) {
        *c->p = (uint8_t)byte;
    }
    c->p++;
}

static void put32(Code *c, uint32_t value) {
    for (int i = 0; i < 4; i++) {
        put(c, value >> (8 * i));
    }
}

static void put64(Code *c, uint64_t value) {
    put32(c, (uint32_t)value);
    put32(c, (uint32_t)(value >> 32));
}

static void rex(Code *c, int wide, int reg, int index, int rm) {
    uint32_t value = 0x40 | (wide ? 8 : 0) | (reg & 8 ? 4 : 0) | (index & 8 ? 2 : 0) | (rm & 8 ? 1 : 0);
    if (value != 0x40) {
        put(c, value);
    }
}

static void opcode(Code *c, uint32_t op) {
    if (op > 0xFF) {
        put(c, op >> 8);
    }
    put(c, op & 0xFF);
}

// The ModRM byte, SIB and displacement for reg and [base + disp]
static void address(Code *c, int reg, int base, int32_t disp) {
    int low = base & 7, mod = disp == 0 && low != 5 ? 0 : disp >= -128 && disp < 128 ? 1 : 2;
    put(c, (uint32_t)(mod << 6 | (reg & 7) << 3 | low));
    if (low == 4) {
        put(c, 0x24);
    }
    if (mod == 1) {
        put(c, (uint32_t)disp & 0xFF);
    } else if (mod == 2) {
        put32(c, (uint32_t)disp);
    }
}

// op reg, [base + disp]
static void op_mem(Code *c, int wide, uint32_t op, int reg, int base, int32_t disp) {
    rex(c, wide, reg, 0, base);
    opcode(c, op);
    address(c, reg, base, disp);
}

// op reg, [base + index << scale]
static void op_index(Code *c, int wide, uint32_t op, int reg, int base, int index, int scale) {
    int low = base & 7;
    rex(c, wide, reg, index, base);
    opcode(c, op);
    put(c, (uint32_t)((low == 5 ? 0x44 : 0x04) | (reg & 7) << 3));
    put(c, (uint32_t)(scale << 6 | (index & 7) << 3 | low));
    if (low == 5) {
        put(c, 0);
    }
}

// op reg, rm with both registers
static void op_reg(Code *c, int wide, uint32_t op, int reg, int rm) {
    rex(c, wide, reg, 0, rm);
    opcode(c, op);
    put(c, (uint32_t)(0xC0 | (reg & 7) << 3 | (rm & 7)));
}

static void load(Code *c, int reg, int base, int32_t disp) {
    op_mem(c, 0, 0x8B, reg, base, disp);
}

static void store(Code *c, int base, int32_t disp, int reg) {
    op_mem(c, 0, 0x89, reg, base, disp);
}

static void store_imm(Code *c, int base, int32_t disp, uint32_t value) {
    op_mem(c, 0, 0xC7, 0, base, disp);
    put32(c, value);
}

static void mov_imm(Code *c, int reg, uint32_t value) {
    if (value == 0) {
        op_reg(c, 0, 0x31, reg, reg);
        return;
    }
    rex(c, 0, 0, 0, reg);
    put(c, 0xB8 + (uint32_t)(reg & 7));
    put32(c, value);
}

static void mov_imm64(Code *c, int reg, uint64_t value) {
    rex(c, 1, 0, 0, reg);
    put(c, 0xB8 + (uint32_t)(reg & 7));
    put64(c, value);
}

static void mov(Code *c, int dst, int src) {
    op_reg(c, 0, 0x89, src, dst);
}

static void mov64(Code *c, int dst, int src) {
    op_reg(c, 1, 0x89, src, dst);
}

static void alu(Code *c, int op, int dst, int src) {
    op_reg(c, 0, (uint32_t)(op * 8 + 1), src, dst);
}

static void alu64(Code *c, int op, int dst, int src) {
    op_reg(c, 1, (uint32_t)(op * 8 + 1), src, dst);
}

static void alu_imm(Code *c, int op, int dst, uint32_t value) {
    if ((int32_t)value >= -128 && (int32_t)value < 128) {
        op_reg(c, 0, 0x83, op, dst);
        put(c, value & 0xFF);
    } else {
        op_reg(c, 0, 0x81, op, dst);
        put32(c, value);
    }
}

// op reg, [base + disp]
static void alu_load(Code *c, int op, int reg, int base, int32_t disp) {
    op_mem(c, 0, (uint32_t)(op * 8 + 3), reg, base, disp);
}

// op dword [base + disp], value
static void alu_mem_imm(Code *c, int op, int base, int32_t disp, uint32_t value) {
    if ((int32_t)value >= -128 && (int32_t)value < 128) {
        op_mem(c, 0, 0x83, op, base, disp);
        put(c, value & 0xFF);
    } else {
        op_mem(c, 0, 0x81, op, base, disp);
        put32(c, value);
    }
}

// lea dst, [base + disp] with 32 or 64 bits of result
static void lea(Code *c, int wide, int dst, int base, int32_t disp) {
    op_mem(c, wide, 0x8D, dst, base, disp);
}

// Shifts and rotations by an immediate: ROL 0, ROR 1, SHL 4, SHR 5, SAR 7
static void shift_imm(Code *c, int kind, int reg, uint32_t count) {
    op_reg(c, 0, 0xC1, kind, reg);
    put(c, count);
}

static void shift_cl(Code *c, int kind, int reg) {
    op_reg(c, 0, 0xD3, kind, reg);
}

// The group of NOT 2, NEG 3, MUL 4, DIV 6 and IDIV 7 on a register
static void unary(Code *c, int kind, int reg) {
    op_reg(c, 0, 0xF7, kind, reg);
}

static void setcc(Code *c, int cc, int reg) {
    op_reg(c, 0, 0x0F90 + (uint32_t)cc, 0, reg);
}

static void movzx8(Code *c, int dst, int src) {
    op_reg(c, 0, 0x0FB6, dst, src);
}

static void call(Code *c, void *function) {
    mov_imm64(c, HAX, (uint64_t)(uintptr_t)function);
    put(c, 0xFF);
    put(c, 0xD0);
}

// A jump whose 32-bit displacement the caller patches; returns where the displacement is
static uint8_t *jcc(Code *c, int cc) {
    put(c, 0x0F);
    put(c, 0x80 + (uint32_t)cc);
    uint8_t *at = c->p;
    put32(c, 0);
    return at;
}

static uint8_t *jmp(Code *c) {
    put(c, 0xE9);
    uint8_t *at = c->p;
    put32(c, 0);
    return at;
}

static void jmp_reg(Code *c, int reg) {
    rex(c, 0, 0, 0, reg);
    put(c, 0xFF);
    put(c, (uint32_t)(0xE0 | (reg & 7)));
}

// Points the displacement at to target, when both lie in the code
static void patch(const Code *c, uint8_t *at, const uint8_t *target) {
    if (at + 4 <= c->end && target <= c->end) {
        write_le32(at, (uint32_t)(target - (at + 4)));
    }
}

static void jmp_to(Code *c, const uint8_t *target) {
    patch(c, jmp(c), target);
}

static void endbr(Code *c) {
    put(c, 0xF3);
    put(c, 0x0F);
    put(c, 0x1E);
    put(c, 0xFA);
}

// The code that enters a block with the context and leaves it again, which compiled code jumps to with the
// word or address in eax and how cpu_run goes on in edx
static void write_entry(struct Jit *jit, Code *c) {
    jit->enter = (Enter)(void *)c->p;
    endbr(c);
    static const int saved[] = { HBP, HBX, H12, H13, H14, H15 };
    for (int i = 0; i < 6; i++) {
        rex(c, 0, 0, 0, saved[i]);
        put(c, 0x50 + (uint32_t)(saved[i] & 7));
    }
    rex(c, 1, 0, 0, HSP);
    put(c, 0x83);
    put(c, 0xEC);
    put(c, FRAME_SIZE);
    op_mem(c, 1, 0x89, HDI, HSP, 8);
    op_mem(c, 1, 0x8B, H12, HDI, (int32_t)offsetof(Context, memory));
    op_mem(c, 1, 0x8B, H13, HDI, (int32_t)offsetof(Context, vm));
    op_mem(c, 1, 0x8B, HAX, HDI, (int32_t)offsetof(Context, entries));
    op_mem(c, 1, 0x89, HAX, HSP, ENTRIES_AT);
    load(c, H14, HDI, (int32_t)offsetof(Context, left));
    for (int32_t at = 0; at < (int32_t)sizeof(Flags); at += 8) {
        op_mem(c, 1, 0x8B, HAX, HDI, (int32_t)offsetof(Context, flags) + at);
        op_mem(c, 1, 0x89, HAX, HSP, FLAGS_AT + at);
    }
    load(c, HBX, H13, REG(R2_SP));
    load(c, HBP, H13, REG(R0_ACC));
    jmp_reg(c, HSI);

    jit->exit = c->p;
    store(c, H13, REG(R2_SP), HBX);
    store(c, H13, REG(R0_ACC), HBP);
    op_mem(c, 1, 0x8B, HDI, HSP, 8);
    store(c, HDI, (int32_t)offsetof(Context, next), HAX);
    store(c, HDI, (int32_t)offsetof(Context, how), HDX);
    store(c, HDI, (int32_t)offsetof(Context, left), H14);
    for (int32_t at = 0; at < (int32_t)sizeof(Flags); at += 8) {
        op_mem(c, 1, 0x8B, HCX, HSP, FLAGS_AT + at);
        op_mem(c, 1, 0x89, HCX, HDI, (int32_t)offsetof(Context, flags) + at);
    }
    rex(c, 1, 0, 0, HSP);
    put(c, 0x83);
    put(c, 0xC4);
    put(c, FRAME_SIZE);
    for (int i = 5; i >= 0; i--) {
        rex(c, 0, 0, 0, saved[i]);
        put(c, 0x58 + (uint32_t)(saved[i] & 7));
    }
    put(c, 0xC3);
}

// Helpers that compiled code calls

static uint32_t holds_for(VM *vm, Flags *flags, uint32_t cond) {
    return (uint32_t)holds(vm->registers, (uint16_t)cond, flags);
}

static void partial_for(Flags *flags, uint32_t result, uint32_t changed, uint32_t bits) {
    FlagUpdate f = { changed, bits };
    partial(result, &f, flags);
}

static uint32_t binary_for(VM *vm, Flags *flags, uint32_t opcode, uint32_t x, uint32_t y) {
    return binary((uint8_t)opcode, vm->registers, x, y, flags);
}

// A heap or copy syscall of supervisor mode as cpu_run runs it at address; tells whether it dropped compiled code
static uint32_t syscall_for(VM *vm, Flags *flags, uint32_t number, uint32_t address, uint32_t next) {
    uint32_t flushes = vm->jit->flushes;
    vm->error_pc = address;
    vm->registers[R3_PC] = next;
    settle(vm->registers, flags);
    syscall_dispatch(vm, number);
    return vm->jit->flushes != flushes;
}

static void push_frame_for(VM *vm, uint32_t site, uint32_t back) {
    cpu_push_frame(vm, site, back, -1);
}

static void pop_frames_for(VM *vm) {
    cpu_pop_frames(vm, 0);
}

// An instruction of a block as the compiler sees it
typedef struct {
    Decoded d;              // with the kind the word had before compiled code took it over
    uint32_t index;
    uint8_t flags_read;     // whether something can read the flags it sets before others replace them
} Item;

enum {
    STUB_BUDGET,            // the budget does not allow the block, so cpu_run runs its first instruction
    STUB_HERE,              // leaves before the instruction, for cpu_run to run it
    STUB_TAKEN,             // the instruction jumps to the word target
    STUB_LEAVE,             // the instruction goes to the address target
    STUB_GO,                // the instruction goes to the address it left at [rsp + GO_SLOT]
    STUB_REACH,             // checks the access of size bytes, extra the access, at edx in the heap or with cpu_reach
    STUB_WRITTEN,           // the store of size bytes at edx changed decoded instructions
    STUB_AFTER,             // leaves after the instruction, which changed decoded instructions or compiled code
    STUB_PUSH,              // asks cpu_push_slot for a push of the value in ecx
    STUB_POP,               // asks cpu_pop_slot for a pop at ecx
    STUB_FRAME,             // the shadow call stack is full
};

typedef struct {
    uint8_t *jump;          // the displacement that leads to the stub
    uint8_t *also;          // for STUB_REACH, the one of an access in the heap
    uint8_t type;
    uint32_t item;
    uint32_t target;
    uint32_t size;
    uint32_t extra;
    uint8_t *resume;
} Stub;

typedef struct {
    struct Jit *jit;
    VM *vm;
    Code c;
    uint32_t count;
    uint32_t stub_count;
    int failed;
    int state;              // the flags as compiled code keeps them at this point, -1 if not known here
    int host;               // the x86 flags hold the flags of the instruction compiled last
    uint32_t end;           // the word after the last instruction
    uint32_t start;
    uint8_t *entry;
    uint32_t link_count;
    Item items[BLOCK_LIMIT];
    Stub stubs[STUB_LIMIT];
    Link links[BLOCK_LIMIT + 1];
} Compiler;

static Stub *stub(Compiler *k, uint8_t *jump, uint8_t type, uint32_t item) {
    if (k->stub_count == STUB_LIMIT) {
        k->failed = 1;
        return &k->stubs[0];
    }
    Stub *s = &k->stubs[k->stub_count++];
    memset(s, 0, sizeof(*s));
    s->jump = jump;
    s->type = type;
    s->item = item;
    return s;
}

static void leave_with(Code *c, struct Jit *jit, uint32_t how) {
    mov_imm(c, HDX, how);
    jmp_to(c, jit->exit);
}

// Goes on with a word: the compiled code there if there is some, else cpu_run
static void chain(Compiler *k, uint32_t word) {
    Code *c = &k->c;
    struct Jit *jit = k->jit;
    if (word == k->start) {
        jmp_to(c, k->entry);
        return;
    }
    if (word < jit->words && jit->entries[word]) {
        jmp_to(c, jit->entries[word]);
        return;
    }
    if (word < jit->words && k->link_count < BLOCK_LIMIT + 1) {
        // Compiling the word later points this jump at its code; until then it leaves
        uint8_t *at = jmp(c);
        patch(c, at, c->p);
        k->links[k->link_count++] = (Link){ word, (uint32_t)(at - jit->code), 0 };
    }
    mov_imm(c, HAX, word);
    leave_with(c, jit, JIT_NEXT);
}

// Goes on at the address in eax
static void go(Compiler *k) {
    Code *c = &k->c;
    mov(c, HCX, HAX);
    shift_imm(c, 1, HCX, 2);
    alu_load(c, CMP, HCX, H13, FIELD(decoded_words));
    uint8_t *out = jcc(c, CC_AE);
    op_mem(c, 1, 0x8B, HDX, HSP, ENTRIES_AT);
    op_index(c, 1, 0x8B, HDX, HDX, HCX, 3);
    op_reg(c, 1, 0x85, HDX, HDX);
    uint8_t *none = jcc(c, CC_E);
    jmp_reg(c, HDX);
    patch(c, none, c->p);
    mov(c, HAX, HCX);
    leave_with(c, k->jit, JIT_NEXT);
    patch(c, out, c->p);
    leave_with(c, k->jit, JIT_LEAVE);
}

static void refund(Code *c, uint32_t count) {
    if (count) {
        alu_imm(c, ADD, H14, count);
    }
}

// The host register that holds a VM register while compiled code runs, or -1 if it stays in memory
static int held(uint32_t number) {
    return number == R2_SP ? HBX : number == R0_ACC ? HBP : -1;
}

static void load_reg(Compiler *k, int host, uint32_t number) {
    if (held(number) >= 0) {
        mov(&k->c, host, held(number));
    } else {
        load(&k->c, host, H13, REG(number));
    }
}

static void store_reg(Compiler *k, uint32_t number, int host) {
    if (held(number) >= 0) {
        mov(&k->c, held(number), host);
    } else {
        store(&k->c, H13, REG(number), host);
    }
}

// (r[b] & mask) + imm into a host register
static void operand(Compiler *k, int host, const Decoded *d) {
    if (!d->mask) {
        mov_imm(&k->c, host, d->imm);
        return;
    }
    load_reg(k, host, d->b);
    if (d->imm) {
        alu_imm(&k->c, ADD, host, d->imm);
    }
}

// Makes rax the host address of size bytes at the address in edx, or leaves before item j if accessing them
// would fault; keeps edx
static void reach(Compiler *k, uint32_t j, uint32_t size, uint32_t access) {
    Code *c = &k->c;
    const Decoded *d = &k->items[j].d;
    uint8_t *stack = NULL;
    if (d->mask && (d->b == R1_BP || d->b == R2_SP)) {
        // What SP or BP points to usually lies on the stack, which needs no other check
        mov(c, HCX, HDX);
        alu_load(c, SUB, HCX, H13, CONTROL(CR_SLO));
        alu_load(c, CMP, HCX, H13, FIELD(stack_span));
        stack = jcc(c, CC_B);
    }
    lea(c, 1, HCX, HDX, (int32_t)size);
    load(c, HSI, H13, FIELD(memory_size));
    op_reg(c, 1, 0x39, HSI, HCX);
    Stub *s = stub(k, jcc(c, CC_A), STUB_REACH, j);
    s->size = size;
    s->extra = access;
    load(c, HSI, H13, CONTROL(CR_HEAPLO));
    op_reg(c, 1, 0x39, HSI, HCX);
    uint8_t *below = jcc(c, CC_BE);
    alu_load(c, CMP, HDX, H13, CONTROL(CR_HEAPHI));
    s->also = jcc(c, CC_B);
    patch(c, below, c->p);
    if (stack) {
        patch(c, stack, c->p);
    }
    op_index(c, 1, 0x8D, HAX, H12, HDX, 0);
    s->resume = c->p;
}

// Loads 4 bytes at the operand of item j into ecx
static void load_operand(Compiler *k, uint32_t j) {
    operand(k, HDX, &k->items[j].d);
    reach(k, j, 4, PROT_READ);
    load(&k->c, HCX, HAX, 0);
}

// The value of the operand of item j into ecx, from memory for the M kinds
static void value_operand(Compiler *k, uint32_t j, int memory) {
    if (memory) {
        load_operand(k, j);
    } else {
        operand(k, HCX, &k->items[j].d);
    }
}

// The flags of a + b or a - b in eax and ecx for cpu_run to find, when something reads them
static void keep_flags(Compiler *k, uint32_t j, int kind) {
    Code *c = &k->c;
    if (!k->items[j].flags_read) {
        k->state = -1;
        return;
    }
    store(c, HSP, FLAGS(a), HAX);
    store(c, HSP, FLAGS(b), HCX);
    store_imm(c, HSP, FLAGS(kind), (uint32_t)kind);
    k->state = kind;
}

// The flags after an instruction that sets Z and N from its result in eax and C and O as edx and ecx say, as
// partial does
static void keep_partial(Compiler *k, uint32_t j) {
    Code *c = &k->c;
    if (!k->items[j].flags_read) {
        k->state = -1;
        return;
    }
    if (k->state == FLAGS_ADD || k->state == FLAGS_SUB) {
        load(c, H8, HSP, FLAGS(a));
        store(c, HSP, FLAGS(sa), H8);
        load(c, H8, HSP, FLAGS(b));
        store(c, HSP, FLAGS(sb), H8);
        store_imm(c, HSP, FLAGS(source), (uint32_t)k->state);
        shift_imm(c, 4, HDX, 16);
        alu(c, OR, HDX, HCX);
        store(c, HSP, FLAGS(b), HDX);
        store_imm(c, HSP, FLAGS(kind), FLAGS_NZ);
        store(c, HSP, FLAGS(a), HAX);
    } else if (k->state == FLAGS_NZ) {
        mov(c, H8, HDX);
        shift_imm(c, 4, H8, 16);
        alu_load(c, OR, H8, HSP, FLAGS(b));
        unary(c, 2, HDX);
        alu(c, AND, H8, HDX);
        alu(c, OR, H8, HCX);
        store(c, HSP, FLAGS(b), H8);
        store(c, HSP, FLAGS(a), HAX);
    } else {
        mov(c, HSI, HAX);
        lea(c, 1, HDI, HSP, FLAGS_AT);
        call(c, (void *)partial_for);
    }
    k->state = FLAGS_NZ;
}

// The x86 condition that matches the jump or SET condition cond of the flags, -1 for none
static int condition_code(uint16_t cond) {
    static const struct { uint8_t opcode; int8_t cc; } table[] = {
        { JZ_OP, CC_E }, { JNZ_OP, CC_NE }, { JN_OP, CC_S }, { JO_OP, CC_O }, { JC_OP, CC_B },
        { JBE_OP, CC_BE }, { JA_OP, CC_A }, { JAE_OP, CC_AE }, { JL_OP, CC_L }, { JGE_OP, CC_GE },
        { JLE_OP, CC_LE }, { JG_OP, CC_G },
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        uint16_t mask = 0;
        for (uint32_t flags = 0; flags < 16; flags++) {
            mask |= (uint16_t)(alu_condition(flags, table[i].opcode) << flags);
        }
        if (mask == cond) {
            return table[i].cc;
        }
    }
    return -1;
}

// Sets the x86 flags to those of the instruction that set the flags last, when the compiler knows it;
// tells whether it could
static int host_flags(Compiler *k) {
    Code *c = &k->c;
    if (k->host) {
        return 1;
    }
    if (k->state != FLAGS_ADD && k->state != FLAGS_SUB) {
        return 0;
    }
    load(c, HAX, HSP, FLAGS(a));
    alu_load(c, k->state == FLAGS_SUB ? CMP : ADD, HAX, HSP, FLAGS(b));
    return 1;
}

// Jumps to the stub when cond holds
static void jump_if(Compiler *k, uint16_t cond, Stub *(*make)(Compiler *, uint8_t *, uint32_t), uint32_t j) {
    Code *c = &k->c;
    int cc = condition_code(cond);
    if (cc >= 0 && host_flags(k)) {
        make(k, jcc(c, cc), j);
        return;
    }
    mov64(c, HDI, H13);
    lea(c, 1, HSI, HSP, FLAGS_AT);
    mov_imm(c, HDX, cond);
    call(c, (void *)holds_for);
    op_reg(c, 0, 0x85, HAX, HAX);
    make(k, jcc(c, CC_NE), j);
}

static Stub *taken_stub(Compiler *k, uint8_t *at, uint32_t j) {
    Stub *s = stub(k, at, STUB_TAKEN, j);
    s->target = k->items[j].d.imm;
    return s;
}

static Stub *leave_stub(Compiler *k, uint8_t *at, uint32_t j) {
    Stub *s = stub(k, at, STUB_LEAVE, j);
    s->target = k->items[j].d.imm;
    return s;
}

static Stub *go_stub(Compiler *k, uint8_t *at, uint32_t j) {
    return stub(k, at, STUB_GO, j);
}

// Pushes the value in ecx, or leaves before item j if the push would fault
static void push(Compiler *k, uint32_t j) {
    Code *c = &k->c;
    lea(c, 0, HDX, HBX, -4);
    mov(c, HSI, HDX);
    alu_load(c, SUB, HSI, H13, CONTROL(CR_SLO));
    alu_load(c, CMP, HSI, H13, FIELD(stack_span));
    Stub *s = stub(k, jcc(c, CC_AE), STUB_PUSH, j);
    op_index(c, 1, 0x8D, HAX, H12, HDX, 0);
    s->resume = c->p;
    store(c, HAX, 0, HCX);
    store_reg(k, R2_SP, HDX);
}

// Makes rax the host address of the word a pop at the address in ecx reads, or leaves before item j
static void pop_slot(Compiler *k, uint32_t j) {
    Code *c = &k->c;
    mov(c, HDX, HCX);
    alu_load(c, SUB, HDX, H13, CONTROL(CR_SLO));
    alu_load(c, CMP, HDX, H13, FIELD(stack_span));
    Stub *s = stub(k, jcc(c, CC_AE), STUB_POP, j);
    op_index(c, 1, 0x8D, HAX, H12, HCX, 0);
    s->resume = c->p;
}

// Notes a call from item j in the shadow call stack, with SP already below the return address
static void push_frame(Compiler *k, uint32_t j) {
    Code *c = &k->c;
    const Item *it = &k->items[j];
    uint32_t site = it->index * 4, back = site + it->d.len * 4u;
    load(c, HAX, H13, FIELD(call_depth));
    alu_imm(c, CMP, HAX, VM_CALL_FRAMES);
    Stub *s = stub(k, jcc(c, CC_E), STUB_FRAME, j);
    mov(c, HCX, HAX);
    shift_imm(c, 4, HCX, 4);
    alu64(c, ADD, HCX, H13);
    int32_t frames = FIELD(call_frames);
    store_imm(c, HCX, frames + (int32_t)offsetof(CallFrame, site), site);
    load_reg(k, HDX, R2_SP);
    store(c, HCX, frames + (int32_t)offsetof(CallFrame, slot), HDX);
    store_imm(c, HCX, frames + (int32_t)offsetof(CallFrame, resume), back);
    store_imm(c, HCX, frames + (int32_t)offsetof(CallFrame, vector), 0xFFFFFFFFu);
    alu_imm(c, ADD, HAX, 1);
    store(c, H13, FIELD(call_depth), HAX);
    s->resume = c->p;
}

// Pushes the return address of the call in item j and notes the call
static void call_push(Compiler *k, uint32_t j) {
    const Item *it = &k->items[j];
    mov_imm(&k->c, HCX, it->index * 4 + it->d.len * 4u);
    push(k, j);
    push_frame(k, j);
}

static int memory_kind(uint8_t kind) {
    switch (kind) {
        case D_LOAD_M: case D_LOADB_M: case D_LOADW_M: case D_ADD_M: case D_SUB_M: case D_CMP_M: case D_TEST_M:
        case D_BINARY_M: case D_DIVIDE_M: case D_JUMP_M: case D_CALL_M: case D_PUSH_M:
            return 1;
        default:
            return 0;
    }
}

// Whether the compiler compiles a kind
static int compiles(const Decoded *d) {
    switch (d->kind) {
        case D_NOP: case D_LOAD_V: case D_LOAD_M: case D_LOADB_V: case D_LOADB_M: case D_LOADW_V: case D_LOADW_M:
        case D_STORE: case D_STOREB: case D_STOREW: case D_LEA: case D_ADD_V: case D_ADD_M: case D_SUB_V:
        case D_SUB_M: case D_CMP_V: case D_CMP_M: case D_TEST_V: case D_TEST_M: case D_BINARY_V: case D_BINARY_M:
        case D_DIVIDE_V: case D_DIVIDE_M: case D_MUL: case D_AND: case D_OR: case D_XOR: case D_SHL: case D_SHR:
        case D_SAR: case D_UNARY: case D_SET: case D_JUMP: case D_JUMP_OUT: case D_JUMP_V: case D_JUMP_M:
        case D_GOTO: case D_JZ: case D_JNZ: case D_JC: case D_JAE: case D_JBE: case D_JA: case D_JL: case D_JGE:
        case D_JLE: case D_JG: case D_CALL: case D_CALL_OUT: case D_CALL_V: case D_CALL_M: case D_RET:
        case D_LOOP: case D_LOOP_OUT: case D_PUSH_V: case D_PUSH_M: case D_POP: case D_ENTER: case D_LEAVE:
        case D_PUSHM: case D_POPM: case D_MEMCPY: case D_MEMSET:
            return 1;
        case D_SYSCALL:
            // The heap and copy syscalls, which cpu_run runs itself in supervisor mode
            return d->imm >= SYS_ALLOC && d->imm <= SYS_MEMCPY;
        default:
            return 0;
    }
}

// Whether an instruction always goes elsewhere, which ends a block
static int ends_block(const Decoded *d) {
    switch (d->kind) {
        case D_GOTO: case D_CALL: case D_CALL_OUT: case D_CALL_V: case D_CALL_M: case D_RET:
            return 1;
        case D_JUMP_OUT: case D_JUMP_V: case D_JUMP_M:
            return d->cond == 0xFFFF;
        default:
            return 0;
    }
}

static int sets_all_flags(uint8_t kind) {
    switch (kind) {
        case D_ADD_V: case D_ADD_M: case D_SUB_V: case D_SUB_M: case D_CMP_V: case D_CMP_M: case D_TEST_V:
        case D_TEST_M: case D_AND: case D_OR: case D_XOR:
            return 1;
        default:
            return 0;
    }
}

static int sets_some_flags(uint8_t kind) {
    switch (kind) {
        case D_BINARY_V: case D_BINARY_M: case D_DIVIDE_V: case D_DIVIDE_M: case D_MUL: case D_SHL: case D_SHR:
        case D_SAR: case D_UNARY:
            return 1;
        default:
            return 0;
    }
}

static int reads_flags(const Decoded *d) {
    switch (d->kind) {
        case D_SET: case D_BINARY_V: case D_BINARY_M:
            return 1;
        case D_JUMP: case D_JUMP_OUT: case D_JUMP_V: case D_JUMP_M: case D_JZ: case D_JNZ: case D_JC: case D_JAE:
        case D_JBE: case D_JA: case D_JL: case D_JGE: case D_JLE: case D_JG:
            return d->cond != 0xFFFF;
        default:
            return 0;
    }
}

// Whether compiled code can leave before the instruction runs, or right after it
static int leaves_before(uint8_t kind) {
    switch (kind) {
        case D_STORE: case D_STOREB: case D_STOREW: case D_DIVIDE_V: case D_CALL: case D_CALL_OUT: case D_CALL_V:
        case D_RET: case D_PUSH_V: case D_POP: case D_ENTER: case D_LEAVE: case D_PUSHM: case D_POPM:
        case D_MEMCPY: case D_MEMSET: case D_SYSCALL:
            return 1;
        default:
            return memory_kind(kind);
    }
}

static int leaves_after(const Decoded *d) {
    switch (d->kind) {
        case D_STORE: case D_STOREB: case D_STOREW: case D_LOOP: case D_LOOP_OUT: case D_MEMCPY: case D_MEMSET:
        case D_SYSCALL:
            return 1;
        default:
            return ends_block(d) || reads_flags(d);
    }
}

// Which instructions set flags that something can read: a jump or SET, or cpu_run after the block, which
// gets the flags wherever the block leaves
static void find_flag_readers(Compiler *k) {
    int live = 1;
    for (uint32_t j = k->count; j-- > 0;) {
        const Decoded *d = &k->items[j].d;
        if (leaves_after(d)) {
            live = 1;
        }
        k->items[j].flags_read = (uint8_t)live;
        if (sets_all_flags(d->kind)) {
            live = 0;
        }
        if (reads_flags(d) || leaves_before(d->kind) || (sets_some_flags(d->kind) && k->items[j].flags_read)) {
            live = 1;
        }
    }
}

static void compile_item(Compiler *k, uint32_t j) {
    Code *c = &k->c;
    const Decoded *d = &k->items[j].d;
    int memory = memory_kind(d->kind);
    int host = 0;

    switch (d->kind) {
        case D_NOP:
            break;
        case D_LOAD_V:
        case D_LEA:
            operand(k, HAX, d);
            store_reg(k, d->a, HAX);
            break;
        case D_LOADB_V:
        case D_LOADW_V:
            operand(k, HAX, d);
            alu_imm(c, AND, HAX, d->kind == D_LOADB_V ? 0xFF : 0xFFFF);
            store_reg(k, d->a, HAX);
            break;
        case D_LOAD_M:
        case D_LOADB_M:
        case D_LOADW_M: {
            uint32_t size = d->kind == D_LOAD_M ? 4 : d->kind == D_LOADB_M ? 1 : 2;
            operand(k, HDX, d);
            reach(k, j, size, PROT_READ);
            op_mem(c, 0, size == 4 ? 0x8B : size == 1 ? 0x0FB6 : 0x0FB7, HAX, HAX, 0);
            store_reg(k, d->a, HAX);
            break;
        }
        case D_STORE:
        case D_STOREB:
        case D_STOREW: {
            uint32_t size = d->kind == D_STORE ? 4 : d->kind == D_STOREB ? 1 : 2;
            operand(k, HDX, d);
            reach(k, j, size, PROT_WRITE);
            load_reg(k, HCX, d->a);
            if (size == 2) {
                put(c, 0x66);
            }
            op_mem(c, 0, size == 1 ? 0x88 : 0x89, HCX, HAX, 0);
            alu_load(c, CMP, HDX, H13, FIELD(decoded_high));
            Stub *s = stub(k, jcc(c, CC_B), STUB_WRITTEN, j);
            s->size = size;
            break;
        }
        case D_ADD_V:
        case D_ADD_M:
        case D_SUB_V:
        case D_SUB_M: {
            int add = d->kind == D_ADD_V || d->kind == D_ADD_M;
            value_operand(k, j, memory);
            load_reg(k, HAX, d->a);
            keep_flags(k, j, add ? FLAGS_ADD : FLAGS_SUB);
            alu(c, add ? ADD : SUB, HAX, HCX);
            store_reg(k, d->a, HAX);
            host = 1;
            break;
        }
        case D_CMP_V:
        case D_CMP_M:
            value_operand(k, j, memory);
            load_reg(k, HAX, d->a);
            keep_flags(k, j, FLAGS_SUB);
            alu(c, CMP, HAX, HCX);
            host = 1;
            break;
        case D_TEST_V:
        case D_TEST_M:
        case D_AND:
        case D_OR:
        case D_XOR:
            value_operand(k, j, memory);
            load_reg(k, HAX, d->a);
            alu(c, d->kind == D_OR ? OR : d->kind == D_XOR ? XOR : AND, HAX, HCX);
            if (d->kind != D_TEST_V && d->kind != D_TEST_M) {
                store_reg(k, d->a, HAX);
            }
            if (k->items[j].flags_read) {
                store(c, HSP, FLAGS(a), HAX);
                store_imm(c, HSP, FLAGS(b), 0);
                store_imm(c, HSP, FLAGS(kind), FLAGS_ADD);
                k->state = FLAGS_ADD;
            } else {
                k->state = -1;
            }
            host = 1;
            break;
        case D_BINARY_V:
        case D_BINARY_M:
            value_operand(k, j, memory);
            mov(c, H8, HCX);
            load_reg(k, HCX, d->a);
            mov_imm(c, HDX, d->op);
            lea(c, 1, HSI, HSP, FLAGS_AT);
            mov64(c, HDI, H13);
            call(c, (void *)binary_for);
            store_reg(k, d->a, HAX);
            k->state = -1;
            break;
        case D_DIVIDE_V:
        case D_DIVIDE_M: {
            int is_signed = d->op == IDIV_OP || d->op == IMOD_OP;
            value_operand(k, j, memory);
            load_reg(k, HAX, d->a);
            op_reg(c, 0, 0x85, HCX, HCX);
            stub(k, jcc(c, CC_E), STUB_HERE, j);
            if (is_signed) {
                alu_imm(c, CMP, HAX, 0x80000000u);
                uint8_t *fine = jcc(c, CC_NE);
                alu_imm(c, CMP, HCX, 0xFFFFFFFFu);
                stub(k, jcc(c, CC_E), STUB_HERE, j);
                patch(c, fine, c->p);
                put(c, 0x99);
                unary(c, 7, HCX);
            } else {
                mov_imm(c, HDX, 0);
                unary(c, 6, HCX);
            }
            if (d->op == MOD_OP || d->op == IMOD_OP) {
                mov(c, HAX, HDX);
            }
            store_reg(k, d->a, HAX);
            if (k->items[j].flags_read) {
                mov_imm(c, HCX, 0);
                mov_imm(c, HDX, 0);
            }
            keep_partial(k, j);
            break;
        }
        case D_MUL:
            operand(k, HCX, d);
            load_reg(k, HAX, d->a);
            unary(c, 4, HCX);
            store_reg(k, d->a, HAX);
            if (k->items[j].flags_read) {
                setcc(c, CC_O, HCX);
                movzx8(c, HCX, HCX);
                shift_imm(c, 4, HCX, 3);
                mov_imm(c, HDX, OVER_FLAG);
            }
            keep_partial(k, j);
            break;
        case D_SHL:
        case D_SHR:
        case D_SAR: {
            int kind = d->kind == D_SHL ? 4 : d->kind == D_SHR ? 5 : 7;
            load_reg(k, HAX, d->a);
            if (!d->mask) {
                uint32_t count = d->imm & 0x1F;
                if (count) {
                    shift_imm(c, kind, HAX, count);
                }
                store_reg(k, d->a, HAX);
                if (k->items[j].flags_read) {
                    if (count) {
                        setcc(c, CC_B, HCX);
                        movzx8(c, HCX, HCX);
                        shift_imm(c, 4, HCX, 2);
                        mov_imm(c, HDX, CARRY_FLAG);
                    } else {
                        mov_imm(c, HCX, 0);
                        mov_imm(c, HDX, 0);
                    }
                }
            } else {
                operand(k, HCX, d);
                alu_imm(c, AND, HCX, 0x1F);
                // edx is C if the count is not 0; the AND clears the carry for a count of 0
                mov(c, HDX, HCX);
                unary(c, 3, HDX);
                op_reg(c, 0, 0x19, HDX, HDX);
                alu_imm(c, AND, HDX, CARRY_FLAG);
                shift_cl(c, kind, HAX);
                store_reg(k, d->a, HAX);
                if (k->items[j].flags_read) {
                    setcc(c, CC_B, HCX);
                    movzx8(c, HCX, HCX);
                    shift_imm(c, 4, HCX, 2);
                }
            }
            keep_partial(k, j);
            break;
        }
        case D_UNARY: {
            int over = d->op == INC_OP || d->op == DEC_OP || d->op == NEG_OP;
            load_reg(k, HAX, d->a);
            switch (d->op) {
                case INC_OP:
                    op_reg(c, 0, 0xFF, 0, HAX);
                    break;
                case DEC_OP:
                    op_reg(c, 0, 0xFF, 1, HAX);
                    break;
                case NEG_OP:
                    unary(c, 3, HAX);
                    break;
                case NOT_OP:
                    unary(c, 2, HAX);
                    break;
                default:
                    put(c, 0x0F);
                    put(c, 0xC8);
                    break;
            }
            store_reg(k, d->a, HAX);
            if (k->items[j].flags_read) {
                if (over) {
                    setcc(c, CC_O, HCX);
                    movzx8(c, HCX, HCX);
                    shift_imm(c, 4, HCX, 3);
                    mov_imm(c, HDX, OVER_FLAG);
                } else {
                    mov_imm(c, HCX, 0);
                    mov_imm(c, HDX, 0);
                }
            }
            keep_partial(k, j);
            break;
        }
        case D_SET: {
            int cc = condition_code(d->cond);
            if (cc >= 0 && host_flags(k)) {
                setcc(c, cc, HAX);
                movzx8(c, HAX, HAX);
            } else {
                mov64(c, HDI, H13);
                lea(c, 1, HSI, HSP, FLAGS_AT);
                mov_imm(c, HDX, d->cond);
                call(c, (void *)holds_for);
            }
            store_reg(k, d->a, HAX);
            break;
        }
        case D_GOTO:
            chain(k, d->imm);
            break;
        case D_JUMP:
        case D_JZ:
        case D_JNZ:
        case D_JC:
        case D_JAE:
        case D_JBE:
        case D_JA:
        case D_JL:
        case D_JGE:
        case D_JLE:
        case D_JG:
            jump_if(k, d->cond, taken_stub, j);
            break;
        case D_JUMP_OUT:
            if (d->cond == 0xFFFF) {
                refund(c, 0);
                mov_imm(c, HAX, d->imm);
                leave_with(c, k->jit, JIT_LEAVE);
            } else {
                jump_if(k, d->cond, leave_stub, j);
            }
            break;
        case D_JUMP_V:
        case D_JUMP_M:
            if (memory) {
                load_operand(k, j);
                mov(c, HAX, HCX);
            } else {
                operand(k, HAX, d);
            }
            if (d->cond == 0xFFFF) {
                go(k);
            } else {
                store(c, HSP, GO_SLOT, HAX);
                k->host = 0;
                jump_if(k, d->cond, go_stub, j);
            }
            break;
        case D_CALL:
            call_push(k, j);
            chain(k, d->imm);
            break;
        case D_CALL_OUT:
            call_push(k, j);
            mov_imm(c, HAX, d->imm);
            leave_with(c, k->jit, JIT_LEAVE);
            break;
        case D_CALL_V:
        case D_CALL_M:
            if (memory) {
                load_operand(k, j);
                mov(c, HAX, HCX);
            } else {
                operand(k, HAX, d);
            }
            store(c, HSP, GO_SLOT, HAX);
            call_push(k, j);
            load(c, HAX, HSP, GO_SLOT);
            go(k);
            break;
        case D_RET:
            load_reg(k, HCX, R2_SP);
            pop_slot(k, j);
            load(c, HAX, HAX, 0);
            alu_imm(c, ADD, HBX, 4 + d->imm);
            store(c, HSP, GO_SLOT, HAX);
            store(c, H13, REG(R2_SP), HBX);
            mov64(c, HDI, H13);
            call(c, (void *)pop_frames_for);
            load(c, HAX, HSP, GO_SLOT);
            go(k);
            break;
        case D_LOOP:
        case D_LOOP_OUT:
            if (held(d->a) >= 0) {
                alu_imm(c, SUB, held(d->a), 1);
            } else {
                alu_mem_imm(c, SUB, H13, REG(d->a), 1);
            }
            if (d->kind == D_LOOP) {
                taken_stub(k, jcc(c, CC_NE), j);
            } else {
                leave_stub(k, jcc(c, CC_NE), j);
            }
            break;
        case D_PUSH_V:
        case D_PUSH_M:
            value_operand(k, j, memory);
            push(k, j);
            break;
        case D_POP:
            load_reg(k, HCX, R2_SP);
            pop_slot(k, j);
            load(c, HDX, HAX, 0);
            alu_imm(c, ADD, HBX, 4);
            store_reg(k, d->a, HDX);
            break;
        case D_ENTER: {
            load_reg(k, HAX, R2_SP);
            lea(c, 0, HCX, HAX, -4);
            mov(c, HDX, HCX);
            alu_load(c, SUB, HDX, H13, CONTROL(CR_SLO));
            alu_imm(c, CMP, HDX, d->imm);
            stub(k, jcc(c, CC_B), STUB_HERE, j);
            alu_load(c, CMP, HDX, H13, FIELD(stack_span));
            Stub *s = stub(k, jcc(c, CC_AE), STUB_PUSH, j);
            op_index(c, 1, 0x8D, HAX, H12, HCX, 0);
            s->resume = c->p;
            load_reg(k, HDX, R1_BP);
            store(c, HAX, 0, HDX);
            store_reg(k, R1_BP, HCX);
            alu_imm(c, SUB, HCX, d->imm);
            store_reg(k, R2_SP, HCX);
            break;
        }
        case D_LEAVE:
            load_reg(k, HCX, R1_BP);
            pop_slot(k, j);
            load_reg(k, HCX, R1_BP);
            lea(c, 0, HDX, HCX, 4);
            store_reg(k, R2_SP, HDX);
            load(c, HDX, HAX, 0);
            store_reg(k, R1_BP, HDX);
            break;
        case D_PUSHM:
        case D_POPM: {
            uint32_t count = d->imm / 4;
            load_reg(k, HAX, R2_SP);
            if (d->kind == D_PUSHM) {
                mov(c, HCX, HAX);
                alu_imm(c, SUB, HCX, d->imm);
                mov(c, HDX, HCX);
            } else {
                mov(c, HDX, HAX);
            }
            alu_load(c, SUB, HDX, H13, CONTROL(CR_SLO));
            alu_load(c, CMP, HDX, H13, FIELD(stack_span));
            stub(k, jcc(c, CC_AE), STUB_HERE, j);
            if (d->kind == D_PUSHM) {
                alu_load(c, CMP, HAX, H13, CONTROL(CR_SHI));
                stub(k, jcc(c, CC_A), STUB_HERE, j);
                op_index(c, 1, 0x8D, HDX, H12, HCX, 0);
                for (uint32_t i = 0; i < count; i++) {
                    load_reg(k, HSI, d->a + i);
                    store(c, HDX, (int32_t)(4 * i), HSI);
                }
                store_reg(k, R2_SP, HCX);
            } else {
                mov(c, HCX, HAX);
                alu_imm(c, ADD, HCX, d->imm);
                alu_load(c, CMP, HCX, H13, CONTROL(CR_SHI));
                stub(k, jcc(c, CC_A), STUB_HERE, j);
                op_index(c, 1, 0x8D, HDX, H12, HAX, 0);
                for (uint32_t i = 0; i < count; i++) {
                    load(c, HSI, HDX, (int32_t)(4 * i));
                    store_reg(k, d->a + i, HSI);
                }
                store_reg(k, R2_SP, HCX);
            }
            break;
        }
        case D_MEMCPY:
        case D_MEMSET:
            if (d->mask) {
                load_reg(k, HCX, d->op);
            } else {
                mov_imm(c, HCX, d->imm);
            }
            load_reg(k, HDX, d->b);
            load_reg(k, HSI, d->a);
            mov_imm(c, H8, d->kind == D_MEMCPY);
            mov64(c, HDI, H13);
            call(c, (void *)cpu_fill);
            alu_imm(c, CMP, HAX, 1);
            stub(k, jcc(c, CC_B), STUB_HERE, j);
            stub(k, jcc(c, CC_A), STUB_AFTER, j);
            break;
        case D_SYSCALL:
            // User mode leaves it to vm_step
            op_mem(c, 0, 0xF7, 0, H13, REG(R4_SR));
            put32(c, SYS_FLAG);
            stub(k, jcc(c, CC_E), STUB_HERE, j);
            store(c, H13, REG(R2_SP), HBX);
            store(c, H13, REG(R0_ACC), HBP);
            mov64(c, HDI, H13);
            lea(c, 1, HSI, HSP, FLAGS_AT);
            mov_imm(c, HDX, d->imm);
            mov_imm(c, HCX, k->items[j].index * 4);
            mov_imm(c, H8, (k->items[j].index + d->len) * 4);
            call(c, (void *)syscall_for);
            load(c, HBX, H13, REG(R2_SP));
            load(c, HBP, H13, REG(R0_ACC));
            op_reg(c, 0, 0x85, HAX, HAX);
            stub(k, jcc(c, CC_NE), STUB_AFTER, j);
            k->state = -1;
            break;
    }
    k->host = host;
}

static void compile_stub(Compiler *k, Stub *s) {
    Code *c = &k->c;
    const Item *it = &k->items[s->item];
    uint32_t n = k->count, after = n - s->item - 1;
    patch(c, s->jump, c->p);
    switch (s->type) {
        case STUB_BUDGET:
            refund(c, n);
            mov_imm(c, HAX, it->index);
            leave_with(c, k->jit, JIT_HERE);
            break;
        case STUB_HERE:
            refund(c, n - s->item);
            mov_imm(c, HAX, it->index);
            leave_with(c, k->jit, JIT_HERE);
            break;
        case STUB_TAKEN:
            refund(c, after);
            chain(k, s->target);
            break;
        case STUB_LEAVE:
            refund(c, after);
            mov_imm(c, HAX, s->target);
            leave_with(c, k->jit, JIT_LEAVE);
            break;
        case STUB_GO:
            refund(c, after);
            load(c, HAX, HSP, GO_SLOT);
            go(k);
            break;
        case STUB_REACH: {
            // An access in the heap goes on when the rights of its first and its last 8 bytes allow it, as in
            // memory_heap_allows; anything else asks cpu_reach
            uint8_t *slow[4];
            slow[0] = jmp(c);
            patch(c, s->also, c->p);
            mov(c, HSI, HDX);
            alu_load(c, SUB, HSI, H13, FIELD(heap_rights_base));
            slow[1] = jcc(c, CC_B);
            lea(c, 0, HCX, HSI, (int32_t)s->size - 1);
            shift_imm(c, 5, HCX, (uint32_t)__builtin_ctz(HEAP_UNIT));
            alu_load(c, CMP, HCX, H13, FIELD(heap_rights_count));
            slow[2] = jcc(c, CC_AE);
            shift_imm(c, 5, HSI, (uint32_t)__builtin_ctz(HEAP_UNIT));
            op_mem(c, 1, 0x8B, HDI, H13, FIELD(heap_rights));
            op_index(c, 0, 0x0FB6, HAX, HDI, HSI, 0);
            op_index(c, 0, 0x0FB6, HCX, HDI, HCX, 0);
            alu(c, AND, HAX, HCX);
            alu_imm(c, AND, HAX, HEAP_UNIT_USED | s->extra);
            alu_imm(c, CMP, HAX, HEAP_UNIT_USED | s->extra);
            slow[3] = jcc(c, CC_NE);
            op_index(c, 1, 0x8D, HAX, H12, HDX, 0);
            jmp_to(c, s->resume);
            for (int i = 0; i < 4; i++) {
                patch(c, slow[i], c->p);
            }
            store(c, HSP, 0, HDX);
            mov64(c, HDI, H13);
            mov(c, HSI, HDX);
            mov_imm(c, HDX, s->size);
            mov_imm(c, HCX, s->extra);
            call(c, (void *)cpu_reach);
            load(c, HDX, HSP, 0);
            op_reg(c, 1, 0x85, HAX, HAX);
            uint8_t *fails = jcc(c, CC_E);
            jmp_to(c, s->resume);
            patch(c, fails, c->p);
            refund(c, n - s->item);
            mov_imm(c, HAX, it->index);
            leave_with(c, k->jit, JIT_HERE);
            break;
        }
        case STUB_WRITTEN:
            mov64(c, HDI, H13);
            mov(c, HSI, HDX);
            mov_imm(c, HDX, s->size);
            call(c, (void *)cpu_forget);
            // fall through
        case STUB_AFTER:
            refund(c, after);
            mov_imm(c, HAX, it->index + it->d.len);
            leave_with(c, k->jit, JIT_NEXT);
            break;
        case STUB_PUSH: {
            // ENTER keeps SP - 4 in ecx, a push the value in ecx and SP - 4 in edx
            store(c, HSP, 0, HCX);
            mov64(c, HDI, H13);
            load_reg(k, HSI, R2_SP);
            call(c, (void *)cpu_push_slot);
            load(c, HCX, HSP, 0);
            load_reg(k, HDX, R2_SP);
            alu_imm(c, SUB, HDX, 4);
            op_reg(c, 1, 0x85, HAX, HAX);
            uint8_t *fails = jcc(c, CC_E);
            jmp_to(c, s->resume);
            patch(c, fails, c->p);
            refund(c, n - s->item);
            mov_imm(c, HAX, it->index);
            leave_with(c, k->jit, JIT_HERE);
            break;
        }
        case STUB_POP: {
            mov64(c, HDI, H13);
            mov(c, HSI, HCX);
            call(c, (void *)cpu_pop_slot);
            op_reg(c, 1, 0x85, HAX, HAX);
            uint8_t *fails = jcc(c, CC_E);
            jmp_to(c, s->resume);
            patch(c, fails, c->p);
            refund(c, n - s->item);
            mov_imm(c, HAX, it->index);
            leave_with(c, k->jit, JIT_HERE);
            break;
        }
        case STUB_FRAME: {
            uint32_t site = it->index * 4;
            store(c, H13, REG(R2_SP), HBX);
            mov64(c, HDI, H13);
            mov_imm(c, HSI, site);
            mov_imm(c, HDX, site + it->d.len * 4u);
            call(c, (void *)push_frame_for);
            jmp_to(c, s->resume);
            break;
        }
    }
}

static void forget_code(struct Jit *jit, VM *vm) {
    jit->flushes++;
    for (uint32_t i = 0; i < jit->start_count; i++) {
        uint32_t word = jit->starts[i];
        jit->entries[word] = NULL;
        if (vm->decoded && word < vm->decoded_words && vm->decoded[word].kind == D_JIT) {
            vm->decoded[word].kind = D_DECODE;
            vm->decoded[word].heat = 0;
        }
    }
    jit->start_count = 0;
    for (uint32_t i = 0; i < jit->link_count; i++) {
        jit->waiting[jit->links[i].word] = 0;
    }
    jit->link_count = 0;
    jit->used = jit->base;
    jit->low = UINT32_MAX;
    jit->high = 0;
}

static struct Jit *jit_for(VM *vm) {
    struct Jit *jit = vm->jit;
    if (jit && jit->words == vm->decoded_words) {
        return jit;
    }
    jit_free(vm);
    jit = calloc(1, sizeof(*jit));
    if (!jit) {
        return NULL;
    }
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_JIT
    flags |= MAP_JIT;
#endif
    void *code = mmap(NULL, CODE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, flags, -1, 0);
    jit->entries = calloc(vm->decoded_words ? vm->decoded_words : 1, sizeof(void *));
    jit->kinds = calloc(vm->decoded_words ? vm->decoded_words : 1, 1);
    jit->waiting = calloc(vm->decoded_words ? vm->decoded_words : 1, sizeof(uint32_t));
    if (code == MAP_FAILED || !jit->entries || !jit->kinds || !jit->waiting) {
        if (code != MAP_FAILED) {
            munmap(code, CODE_SIZE);
        }
        free(jit->entries);
        free(jit->kinds);
        free(jit->waiting);
        free(jit);
        return NULL;
    }
    jit->code = code;
    jit->words = vm->decoded_words;
    Code c = { jit->code, jit->code + CODE_SIZE };
    write_entry(jit, &c);
    jit->base = jit->used = (size_t)(c.p - jit->code + 15) & ~(size_t)15;
    jit->low = UINT32_MAX;
    vm->jit = jit;
    return jit;
}

static int add_start(struct Jit *jit, uint32_t word) {
    if (jit->start_count == jit->start_room) {
        uint32_t room = jit->start_room ? 2 * jit->start_room : 256;
        uint32_t *starts = realloc(jit->starts, room * sizeof(*starts));
        if (!starts) {
            return 0;
        }
        jit->starts = starts;
        jit->start_room = room;
    }
    jit->starts[jit->start_count++] = word;
    return 1;
}

// Compiles a block from a word into the free code; returns its entry, or NULL if nothing there compiles or
// the code is full, which full tells
static uint8_t *compile_block(struct Jit *jit, VM *vm, uint32_t word, uint32_t *after, int *full) {
    static Compiler k;
    memset(&k, 0, offsetof(Compiler, items));
    *full = 0;
    k.jit = jit;
    k.vm = vm;
    k.state = -1;
    uint32_t index = word;
    *after = UINT32_MAX;
    while (k.count < BLOCK_LIMIT && index < vm->decoded_words) {
        Decoded *d = cpu_decoded_at(vm, index);
        if (d->kind == D_JIT && k.count) {
            break;
        }
        Item *it = &k.items[k.count];
        it->d = *d;
        it->index = index;
        if (d->kind == D_JIT) {
            it->d.kind = jit->kinds[index];
        }
        if (!compiles(&it->d)) {
            *after = index + d->len;
            break;
        }
        k.count++;
        index += d->len;
        if (ends_block(&it->d)) {
            break;
        }
    }
    if (!k.count) {
        return NULL;
    }
    k.end = index;
    find_flag_readers(&k);

    k.c.p = jit->code + jit->used;
    k.c.end = jit->code + CODE_SIZE;
    uint8_t *entry = k.c.p;
    k.start = word;
    k.entry = entry;
    endbr(&k.c);
    alu_imm(&k.c, SUB, H14, k.count);
    stub(&k, jcc(&k.c, CC_B), STUB_BUDGET, 0);
    for (uint32_t j = 0; j < k.count; j++) {
        compile_item(&k, j);
    }
    if (!ends_block(&k.items[k.count - 1].d)) {
        chain(&k, k.end);
    }
    for (uint32_t i = 0; i < k.stub_count; i++) {
        compile_stub(&k, &k.stubs[i]);
    }
    if (k.failed || k.c.p > k.c.end) {
        *full = 1;
        return NULL;
    }
    for (uint32_t i = 0; i < k.link_count; i++) {
        if (jit->link_count == jit->link_room) {
            uint32_t room = jit->link_room ? 2 * jit->link_room : 256;
            Link *links = realloc(jit->links, room * sizeof(*links));
            if (!links) {
                break;
            }
            jit->links = links;
            jit->link_room = room;
        }
        Link *link = &jit->links[jit->link_count++];
        *link = k.links[i];
        link->next = jit->waiting[link->word];
        jit->waiting[link->word] = jit->link_count;
    }
    jit->used = ((size_t)(k.c.p - jit->code) + 15) & ~(size_t)15;
    uint32_t first = word * 4, last = index * 4 + 4;
    jit->low = first < jit->low ? first : jit->low;
    jit->high = last > jit->high ? last : jit->high;
    return entry;
}

int jit_compile(VM *vm, uint32_t index) {
    struct Jit *jit = vm->jit_threshold ? jit_for(vm) : NULL;
    if (!jit || index >= jit->words || jit->entries[index]) {
        return jit && index < jit->words && jit->entries[index];
    }
    for (int depth = 0; depth < 4 && index < jit->words && !jit->entries[index]; depth++) {
        uint32_t after;
        int full;
        uint8_t *entry = compile_block(jit, vm, index, &after, &full);
        if (!entry && full && jit->used > jit->base) {
            forget_code(jit, vm);
            entry = compile_block(jit, vm, index, &after, &full);
        }
        if (!entry || !add_start(jit, index)) {
            return depth > 0;
        }
        Decoded *d = vm->decoded + index;
        jit->kinds[index] = d->kind;
        jit->entries[index] = entry;
        d->kind = D_JIT;
        for (uint32_t i = jit->waiting[index]; i; i = jit->links[i - 1].next) {
            uint8_t *at = jit->code + jit->links[i - 1].at;
            write_le32(at, (uint32_t)(entry - (at + 4)));
        }
        jit->waiting[index] = 0;
        // The instructions after one that the block stops at only run when cpu_run comes back from that one,
        // so they get compiled now
        index = after;
    }
    return 1;
}

uint32_t jit_run(VM *vm, uint32_t index, uint32_t *left, Flags *flags, uint32_t *how) {
    struct Jit *jit = vm->jit;
    Context context = { vm->memory, vm, jit->entries, *flags, *left, 0, 0 };
    jit->enter(&context, jit->entries[index]);
    *left = context.left;
    *flags = context.flags;
    *how = context.how;
    return context.next;
}

uint8_t jit_kind(const VM *vm, uint32_t index) {
    return vm->jit->kinds[index];
}

void jit_written(VM *vm, uint32_t address, uint32_t size) {
    struct Jit *jit = vm->jit;
    if (jit->start_count && address < jit->high && (uint64_t)address + size > jit->low) {
        forget_code(jit, vm);
    }
}

void jit_free(VM *vm) {
    struct Jit *jit = vm->jit;
    if (!jit) {
        return;
    }
    if (vm->decoded) {
        forget_code(jit, vm);
    }
    munmap(jit->code, CODE_SIZE);
    free(jit->entries);
    free(jit->kinds);
    free(jit->starts);
    free(jit->waiting);
    free(jit->links);
    free(jit);
    vm->jit = NULL;
}

#else

int jit_compile(VM *vm, uint32_t index) {
    (void)vm;
    (void)index;
    return 0;
}

uint32_t jit_run(VM *vm, uint32_t index, uint32_t *left, Flags *flags, uint32_t *how) {
    (void)vm;
    (void)left;
    (void)flags;
    *how = JIT_HERE;
    return index;
}

uint8_t jit_kind(const VM *vm, uint32_t index) {
    (void)vm;
    (void)index;
    return D_STOP;
}

void jit_written(VM *vm, uint32_t address, uint32_t size) {
    (void)vm;
    (void)address;
    (void)size;
}

void jit_free(VM *vm) {
    (void)vm;
}

#endif
