#include <stdlib.h>
#include <string.h>
#include "alu.h"
#include "binfmt.h"
#include "cpu.h"
#include "memory.h"
#include "syscalls.h"
#include "vm.h"

// cpu_run executes the common instructions without the decoding, copies and checks that vm_step spends
// on every instruction. It stops before an instruction that would fault or that needs more than it
// does, such as most syscalls, a device, a privileged instruction or a write to PC or SR as a register, and
// leaves that one to vm_step, which does the same as it would.
//
// It decodes an instruction the first time it runs at an address below the heap and keeps what it found
// until something writes to the words of the instruction.
//
// The Z, N, C and O flags wait until something reads them: instructions keep the operands of their sum or
// difference, or the result they set Z and N from. The flags are in SR whenever cpu_run stops or makes a
// syscall, so vm_step, the debugger and the monitor always find them there.

enum {
    STOP,
    RUN_NOP,
    RUN_LOAD,
    RUN_LOADB,
    RUN_LOADW,
    RUN_STORE,
    RUN_STOREB,
    RUN_STOREW,
    RUN_LEA,
    RUN_MOVE,
    RUN_ADD,
    RUN_SUB,
    RUN_CMP,
    RUN_TEST,
    RUN_BINARY,
    RUN_DIVIDE,
    RUN_UNARY,
    RUN_COUNT,
    RUN_FLOAT,
    RUN_FLOAT_SIGN,
    RUN_SET,
    RUN_JUMP,
    RUN_CALL,
    RUN_RET,
    RUN_LOOP,
    RUN_PUSH,
    RUN_POP,
    RUN_ENTER,
    RUN_LEAVE,
    RUN_PUSHM,
    RUN_POPM,
    RUN_MEMCPY,
    RUN_MEMSET,
    RUN_SYSCALL,
};

static const uint8_t runs[256] = {
    [NOP_OP] = RUN_NOP,       [LOAD_OP] = RUN_LOAD,     [LOADB_OP] = RUN_LOADB,   [LOADW_OP] = RUN_LOADW,
    [STORE_OP] = RUN_STORE,   [STOREB_OP] = RUN_STOREB, [STOREW_OP] = RUN_STOREW, [LEA_OP] = RUN_LEA,
    [MOVE_OP] = RUN_MOVE,     [ADD_OP] = RUN_ADD,       [SUB_OP] = RUN_SUB,       [CMP_OP] = RUN_CMP,
    [TEST_OP] = RUN_TEST,     [ADDC_OP] = RUN_BINARY,   [SUBC_OP] = RUN_BINARY,   [MUL_OP] = RUN_BINARY,
    [MULH_OP] = RUN_BINARY,   [UMULH_OP] = RUN_BINARY,  [AND_OP] = RUN_BINARY,    [OR_OP] = RUN_BINARY,
    [XOR_OP] = RUN_BINARY,    [SHL_OP] = RUN_BINARY,    [SHR_OP] = RUN_BINARY,    [SAR_OP] = RUN_BINARY,
    [ROL_OP] = RUN_BINARY,    [ROR_OP] = RUN_BINARY,    [DIV_OP] = RUN_DIVIDE,    [MOD_OP] = RUN_DIVIDE,
    [IDIV_OP] = RUN_DIVIDE,   [IMOD_OP] = RUN_DIVIDE,   [INC_OP] = RUN_UNARY,     [DEC_OP] = RUN_UNARY,
    [NEG_OP] = RUN_UNARY,     [NOT_OP] = RUN_UNARY,     [BSWAP_OP] = RUN_UNARY,   [POPCNT_OP] = RUN_COUNT,
    [CLZ_OP] = RUN_COUNT,     [CTZ_OP] = RUN_COUNT,     [FADD_OP] = RUN_FLOAT,    [FSUB_OP] = RUN_FLOAT,
    [FMUL_OP] = RUN_FLOAT,    [FDIV_OP] = RUN_FLOAT,    [FCMP_OP] = RUN_FLOAT,    [FSQRT_OP] = RUN_FLOAT,
    [ITOF_OP] = RUN_FLOAT,    [FTOI_OP] = RUN_FLOAT,    [FNEG_OP] = RUN_FLOAT_SIGN, [FABS_OP] = RUN_FLOAT_SIGN,
    [SET_OP] = RUN_SET,       [JMP_OP] = RUN_JUMP,      [JZ_OP] = RUN_JUMP,       [JNZ_OP] = RUN_JUMP,
    [JN_OP] = RUN_JUMP,       [JP_OP] = RUN_JUMP,       [JO_OP] = RUN_JUMP,       [JC_OP] = RUN_JUMP,
    [JBE_OP] = RUN_JUMP,      [JA_OP] = RUN_JUMP,       [JL_OP] = RUN_JUMP,       [JGE_OP] = RUN_JUMP,
    [JLE_OP] = RUN_JUMP,      [JG_OP] = RUN_JUMP,       [JAE_OP] = RUN_JUMP,      [CALL_OP] = RUN_CALL,
    [RET_OP] = RUN_RET,       [LOOP_OP] = RUN_LOOP,     [PUSH_OP] = RUN_PUSH,     [POP_OP] = RUN_POP,
    [ENTER_OP] = RUN_ENTER,   [LEAVE_OP] = RUN_LEAVE,   [PUSHM_OP] = RUN_PUSHM,   [POPM_OP] = RUN_POPM,
    [MEMCPY_OP] = RUN_MEMCPY, [MEMSET_OP] = RUN_MEMSET, [SYSCALL_OP] = RUN_SYSCALL,
};

#define EXTENDED_BIT (MODE_EXTENDED << 20)

// The helpers become part of each instruction they serve, where the field and width they get are constants
#ifdef __GNUC__
#define INLINE static inline __attribute__((always_inline))
#define OUTLINE static __attribute__((noinline))
#else
#define INLINE static inline
#define OUTLINE static
#endif

// The fields of the registers that hold or point at an operand
#define REG1 16
#define REG2 12

// What to do with each instruction, by the opcode, extension bit and mode in the top 12 bits of its first
// word; the instructions and modes that vm_step would refuse stop
static uint8_t kinds[4096];

// For each jump opcode, one bit for each value of the Z, N, C and O flags under which it jumps, and for those
// that only depend on Z, C and whether N and O differ, 0x100 and one bit for each value of those three
static uint16_t conditions[256];
static uint16_t quick_conditions[256];

static void prepare(void) {
    for (uint32_t key = 0; key < 4096; key++) {
        const InstructionInfo *info = isa_by_opcode((uint8_t)(key >> 4));
        uint32_t mode = MODE_BIT(key & 7), extended = key & MODE_EXTENDED;
        // A syscall checks the mode itself
        if (!info || (info->privileged && runs[key >> 4] != RUN_SYSCALL) || (info->modes && !(info->modes & mode)) ||
            (extended && !(info->modes & mode & MODES_WITH_IMMEDIATE))) {
            continue;
        }
        kinds[key] = runs[key >> 4];
    }
    for (uint32_t opcode = 0; opcode < 256; opcode++) {
        int seen[8] = { -1, -1, -1, -1, -1, -1, -1, -1 }, quick = 1;
        for (uint32_t flags = 0; flags < 16; flags++) {
            int holds = alu_condition(flags, (uint8_t)opcode);
            uint32_t zero = (flags & ZERO_FLAG) != 0, carry = (flags & CARRY_FLAG) != 0;
            uint32_t less = ((flags & NEG_FLAG) != 0) != ((flags & OVER_FLAG) != 0);
            uint32_t index = zero | carry << 1 | less << 2;
            conditions[opcode] |= (uint16_t)(holds << flags);
            quick &= seen[index] < 0 || seen[index] == holds;
            seen[index] = holds;
        }
        uint32_t table = 0x100;
        for (uint32_t index = 0; index < 8; index++) {
            table |= (uint32_t)(seen[index] > 0) << index;
        }
        quick_conditions[opcode] = quick ? (uint16_t)table : 0;
    }
}


// What cpu_run does with a decoded instruction. The operand of a V kind is a value and that of the M kind
// after it is in memory.
enum {
    D_DECODE,
    D_STOP,
    D_NOP,
    D_LOAD_V, D_LOAD_M, D_LOADB_V, D_LOADB_M, D_LOADW_V, D_LOADW_M,
    D_STORE, D_STOREB, D_STOREW, D_LEA,
    D_ADD_V, D_ADD_M, D_SUB_V, D_SUB_M, D_CMP_V, D_CMP_M, D_TEST_V, D_TEST_M,
    D_BINARY_V, D_BINARY_M, D_DIVIDE_V, D_DIVIDE_M, D_COUNT_V, D_COUNT_M, D_FLOAT_V, D_FLOAT_M,
    D_MUL, D_AND, D_OR, D_XOR, D_SHL, D_SHR, D_SAR,
    D_UNARY, D_FLOAT_SIGN, D_SET,
    D_JUMP, D_JUMP_OUT, D_JUMP_V, D_JUMP_M,
    D_GOTO, D_JZ, D_JNZ, D_JC, D_JAE, D_JBE, D_JA, D_JL, D_JGE, D_JLE, D_JG,
    D_CALL, D_CALL_OUT, D_CALL_V, D_CALL_M, D_RET,
    D_LOOP, D_LOOP_OUT,
    D_PUSH_V, D_PUSH_M, D_POP, D_ENTER, D_LEAVE, D_PUSHM, D_POPM, D_MEMCPY, D_MEMSET, D_SYSCALL,
};

// An instruction as cpu_run runs it. Its operand, or the address of its operand, is (r[b] & mask) + imm.
struct Decoded {
    uint8_t kind;       // 0 until the words at its address are decoded
    uint8_t a;          // the register of the first field
    uint8_t b;          // the register of the operand, or the base of its address
    uint8_t op;         // the opcode for the kinds that several share, or the register of a block size
    uint32_t mask;      // keeps register b, or leaves only imm
    uint32_t imm;       // the immediate or displacement, or the word a jump goes to
    uint16_t cond;      // one bit for each value of the Z, N, C and O flags under which a jump or SET holds
    uint8_t len;        // the words the instruction takes
};

typedef struct Decoded Decoded;

INLINE uint32_t signed16(uint32_t w) {
    return ((w & 0xFFFF) ^ 0x8000) - 0x8000;
}

INLINE uint32_t signed12(uint32_t w) {
    return ((w & 0x0FFF) ^ 0x800) - 0x800;
}

// The extension word follows the instruction word at code
INLINE uint32_t extension(const uint8_t *code) {
    return read_le32(code + 4);
}

// The immediate of IMM, STK and BAS operands, and of instructions that only take one
INLINE uint32_t immediate(uint32_t w, const uint8_t *code) {
    return w & EXTENDED_BIT ? extension(code) : signed16(w);
}

INLINE uint32_t reg1(uint32_t w) {
    return (w >> REG1) & 0x0F;
}

INLINE int writes_special(uint32_t reg) {
    return reg == R3_PC || reg == R4_SR;
}

// Finds the host address of size bytes at address; returns 0 if accessing them would fault. cpu_run runs
// without paging.
INLINE int reach(VM *vm, uint32_t address, uint32_t size, uint8_t access, uint8_t **host) {
    uint64_t end = (uint64_t)address + size;
    *host = vm->memory + address;
    if (end <= vm->memory_size && (end <= vm->control[CR_HEAPLO] || address >= vm->control[CR_HEAPHI] ||
                                   (size <= HEAP_UNIT && memory_heap_allows(vm, address, size, access)))) {
        return 1;
    }
    return (*host = memory_direct(vm, address, size, access)) != NULL;
}

// The stack lies between SLO and SHI. When it lies in memory above the heap and the decoded words, which only
// instructions that cpu_run leaves to vm_step can move, a push only needs SP - 4 - SLO and a pop SP - SLO to be
// below the stack span this returns. Other stacks get the span 0 and the checks of push_slot and pop_slot.
static uint32_t stack_span(const VM *vm) {
    uint32_t low = vm->control[CR_SLO], high = vm->control[CR_SHI];
    if (low < vm->control[CR_HEAPHI] || low < vm->decoded_end || high > vm->memory_size || high < low ||
        high - low < 4) {
        return 0;
    }
    return high - low - 3;
}

INLINE void written(VM *vm, uint32_t address, uint32_t size) {
    if (address < vm->decoded_high) {
        cpu_forget(vm, address, size);
    }
}

// Where a push at sp writes, or NULL if it would fault
OUTLINE uint8_t *push_slot(VM *vm, uint32_t sp) {
    uint32_t low = vm->control[CR_SLO];
    if (sp < low || sp > vm->control[CR_SHI] || sp - low < 4) {
        return NULL;
    }
    uint8_t *slot;
    if (!reach(vm, sp - 4, 4, PROT_WRITE, &slot)) {
        return NULL;
    }
    written(vm, sp - 4, 4);
    return slot;
}

OUTLINE uint8_t *pop_slot(VM *vm, uint32_t sp) {
    uint32_t high = vm->control[CR_SHI];
    if (sp < vm->control[CR_SLO] || sp > high || high - sp < 4) {
        return NULL;
    }
    uint8_t *slot;
    return reach(vm, sp, 4, PROT_READ, &slot) ? slot : NULL;
}

// The number of words below the heap and the end of memory that start an instruction whose extension word
// can be fetched without checks as well
static uint32_t fetch_words(const VM *vm) {
    uint32_t end = vm->control[CR_HEAPLO] < vm->memory_size ? vm->control[CR_HEAPLO] : vm->memory_size;
    return end >= 8 ? (end - 4) / 4 : 0;
}

// The word of an aligned address; other addresses give numbers above any word of memory
INLINE uint32_t word_index(uint32_t address) {
    return (address >> 2) | (address << 30);
}

void cpu_forget(VM *vm, uint32_t address, uint32_t size) {
    if (address >= vm->decoded_high || size == 0) {
        return;
    }
    uint32_t first = address >> 2;
    uint64_t last = ((uint64_t)address + size - 1) >> 2;
    // An instruction that starts in the word before takes the first word as its extension
    if (first > 0) {
        first--;
    }
    if (last > vm->decoded_words) {
        last = vm->decoded_words;
    }
    for (uint32_t k = first; k <= last; k++) {
        if (vm->decoded[k].kind != D_DECODE) {
            vm->decoded[k].kind = D_DECODE;
        }
    }
}

void cpu_forget_all(VM *vm) {
    free(vm->decoded);
    vm->decoded = NULL;
    vm->decoded_words = 0;
    vm->decoded_end = 0;
    vm->decoded_high = 0;
}

// Sets the operand in the register field at field as register b, mask and imm, where PC reads as the address
// of the next instruction; tells whether the operand is in memory
static int decode_operand(Decoded *d, uint32_t w, const uint8_t *code, int field, uint32_t next) {
    uint32_t mode = (w >> 20) & 7;
    d->b = (uint8_t)((w >> field) & 0x0F);
    d->mask = ~0u;
    d->imm = 0;
    switch (mode) {
        case IMM_MODE:
            d->mask = 0;
            d->imm = immediate(w, code);
            break;
        case MEM_MODE:
            d->mask = 0;
            d->imm = w & EXTENDED_BIT ? extension(code) : w & 0xFFFF;
            break;
        case IDX_MODE:
            d->imm = w & EXTENDED_BIT ? extension(code) : signed12(w);
            break;
        case STK_MODE:
            d->b = R2_SP;
            d->imm = immediate(w, code);
            break;
        case BAS_MODE:
            d->b = R1_BP;
            d->imm = immediate(w, code);
            break;
    }
    if (d->mask && d->b == R3_PC) {
        d->mask = 0;
        d->imm += next;
    }
    return mode != IMM_MODE && mode != REG_MODE;
}

// An immediate target goes in imm as a word with the kind in, or as an address with the kind out when it
// lies where cpu_run does not run
static void decode_target(Decoded *d, uint32_t target, uint32_t words, uint8_t in, uint8_t out) {
    uint32_t index = word_index(target);
    d->kind = index < words ? in : out;
    d->imm = index < words ? index : target;
}

// The kind of a jump to a word where cpu_run runs: JMP and the jumps whose condition a compare decides have
// their own
static uint8_t jump_kind(uint8_t opcode) {
    switch (opcode) {
        case JMP_OP:
            return D_GOTO;
        case JZ_OP:
            return D_JZ;
        case JNZ_OP:
            return D_JNZ;
        case JC_OP:
            return D_JC;
        case JAE_OP:
            return D_JAE;
        case JBE_OP:
            return D_JBE;
        case JA_OP:
            return D_JA;
        case JL_OP:
            return D_JL;
        case JGE_OP:
            return D_JGE;
        case JLE_OP:
            return D_JLE;
        case JG_OP:
            return D_JG;
        default:
            return D_JUMP;
    }
}

// The kind of a binary instruction whose operand is a value: its own for the common ones
static uint8_t value_kind(uint8_t opcode) {
    switch (opcode) {
        case MUL_OP:
            return D_MUL;
        case AND_OP:
            return D_AND;
        case OR_OP:
            return D_OR;
        case XOR_OP:
            return D_XOR;
        case SHL_OP:
            return D_SHL;
        case SHR_OP:
            return D_SHR;
        case SAR_OP:
            return D_SAR;
        default:
            return D_BINARY_V;
    }
}

// Decodes the instruction at a word. Instructions that write PC or SR as a register or read PC from their
// first field stop, as do those that cpu_run does not run, and vm_step runs them.
static void decode(VM *vm, Decoded *d, uint32_t index, uint32_t words) {
    memset(d, 0, sizeof(*d));
    d->kind = D_STOP;
    d->len = 1;
    if (index >= words) {
        return;
    }
    uint32_t pc = index * 4;
    const uint8_t *code = vm->memory + pc;
    uint32_t w = read_le32(code), next = pc + 4 + ((w >> 21) & 4), a = reg1(w), value;
    uint8_t run = kinds[w >> 20];
    int memory;

    d->len = (uint8_t)((next - pc) / 4);
    if (next > vm->decoded_high) {
        vm->decoded_high = next;
    }
    d->a = (uint8_t)a;
    d->op = (uint8_t)(w >> 24);
    switch (run) {
        case RUN_NOP:
            d->kind = D_NOP;
            break;
        case RUN_LOAD:
        case RUN_LOADB:
        case RUN_LOADW:
            if (!writes_special(a)) {
                memory = decode_operand(d, w, code, REG2, next);
                d->kind = (uint8_t)((run == RUN_LOAD ? D_LOAD_V : run == RUN_LOADB ? D_LOADB_V : D_LOADW_V) + memory);
            }
            break;
        case RUN_STORE:
        case RUN_STOREB:
        case RUN_STOREW:
            if (a != R3_PC) {
                decode_operand(d, w, code, REG2, next);
                d->kind = run == RUN_STORE ? D_STORE : run == RUN_STOREB ? D_STOREB : D_STOREW;
            }
            break;
        case RUN_LEA:
            if (!writes_special(a)) {
                decode_operand(d, w, code, REG2, next);
                d->kind = D_LEA;
            }
            break;
        case RUN_MOVE:
            if (!writes_special(a)) {
                d->b = (uint8_t)((w >> REG2) & 0x0F);
                d->mask = d->b == R3_PC ? 0 : ~0u;
                d->imm = d->b == R3_PC ? next : 0;
                d->kind = D_LOAD_V;
            }
            break;
        case RUN_ADD:
        case RUN_SUB:
            if (!writes_special(a)) {
                memory = decode_operand(d, w, code, REG2, next);
                d->kind = (uint8_t)((run == RUN_ADD ? D_ADD_V : D_SUB_V) + memory);
            }
            break;
        case RUN_CMP:
        case RUN_TEST:
            if (a != R3_PC) {
                memory = decode_operand(d, w, code, REG2, next);
                d->kind = (uint8_t)((run == RUN_CMP ? D_CMP_V : D_TEST_V) + memory);
            }
            break;
        case RUN_BINARY:
            if (!writes_special(a)) {
                memory = decode_operand(d, w, code, REG2, next);
                d->kind = memory ? D_BINARY_M : value_kind(d->op);
            }
            break;
        case RUN_DIVIDE:
        case RUN_COUNT:
        case RUN_FLOAT:
            if (!writes_special(a)) {
                memory = decode_operand(d, w, code, REG2, next);
                d->kind = (uint8_t)((run == RUN_DIVIDE  ? D_DIVIDE_V
                                     : run == RUN_COUNT ? D_COUNT_V
                                                        : D_FLOAT_V) + memory);
            }
            break;
        case RUN_UNARY:
        case RUN_FLOAT_SIGN:
            if (!writes_special(a)) {
                d->kind = run == RUN_UNARY ? D_UNARY : D_FLOAT_SIGN;
            }
            break;
        case RUN_SET:
            value = immediate(w, code);
            if (!writes_special(a) && value <= 0xFF && isa_is_conditional_jump((uint8_t)value)) {
                d->cond = conditions[value];
                d->kind = D_SET;
            }
            break;
        case RUN_JUMP:
        case RUN_CALL:
            memory = decode_operand(d, w, code, REG1, next);
            d->cond = conditions[d->op];
            if (((w >> 20) & 7) == IMM_MODE && run == RUN_JUMP) {
                decode_target(d, next + d->imm, words, jump_kind(d->op), D_JUMP_OUT);
            } else if (((w >> 20) & 7) == IMM_MODE) {
                decode_target(d, next + d->imm, words, D_CALL, D_CALL_OUT);
            } else {
                d->kind = (uint8_t)((run == RUN_JUMP ? D_JUMP_V : D_CALL_V) + memory);
            }
            break;
        case RUN_RET:
            d->imm = immediate(w, code);
            d->kind = D_RET;
            break;
        case RUN_LOOP:
            if (!writes_special(a) && ((w >> 20) & 7) == IMM_MODE) {
                decode_target(d, next + immediate(w, code), words, D_LOOP, D_LOOP_OUT);
            }
            break;
        case RUN_PUSH:
            memory = decode_operand(d, w, code, REG1, next);
            d->kind = (uint8_t)(D_PUSH_V + memory);
            break;
        case RUN_POP:
            if (!writes_special(a)) {
                d->kind = D_POP;
            }
            break;
        case RUN_ENTER:
            d->imm = immediate(w, code);
            d->kind = D_ENTER;
            break;
        case RUN_LEAVE:
            d->kind = D_LEAVE;
            break;
        case RUN_PUSHM:
        case RUN_POPM:
            // Ranges with SP, PC or SR in them are left to vm_step. The operand is the number of bytes.
            d->b = (uint8_t)((w >> REG2) & 0x0F);
            d->imm = 4 * (d->b - a + 1);
            if (a <= d->b && (a > R4_SR || d->b < R2_SP)) {
                d->kind = run == RUN_PUSHM ? D_PUSHM : D_POPM;
            }
            break;
        case RUN_MEMCPY:
        case RUN_MEMSET:
            // The size is an immediate or the register in the lowest bits
            d->b = (uint8_t)((w >> REG2) & 0x0F);
            d->op = ((w >> 20) & 7) == REG_MODE ? (uint8_t)(w & 0x0F) : 0;
            d->mask = ((w >> 20) & 7) == REG_MODE ? ~0u : 0;
            d->imm = ((w >> 20) & 7) == REG_MODE ? 0 : w & EXTENDED_BIT ? extension(code) : w & 0x0FFF;
            if (a != R3_PC && d->b != R3_PC && (!d->mask || d->op != R3_PC)) {
                d->kind = run == RUN_MEMCPY ? D_MEMCPY : D_MEMSET;
            }
            break;
        case RUN_SYSCALL:
            d->imm = immediate(w, code);
            d->kind = D_SYSCALL;
            break;
    }
    // SR read as a register needs the flags that cpu_run keeps apart, which vm_step finds in SR
    int reads_sr = d->mask && d->b == R4_SR;
    if (run == RUN_STORE || run == RUN_STOREB || run == RUN_STOREW || run == RUN_CMP || run == RUN_TEST) {
        reads_sr |= a == R4_SR;
    } else if (run == RUN_MEMCPY || run == RUN_MEMSET) {
        reads_sr = a == R4_SR || d->b == R4_SR || (d->mask && d->op == R4_SR);
    }
    if (reads_sr) {
        d->kind = D_STOP;
    }
}

INLINE uint32_t operand(const uint32_t *r, const Decoded *d) {
    return (r[d->b] & d->mask) + d->imm;
}

// Reads width bytes at address into value; returns 0 if reading them would fault
INLINE int load(VM *vm, uint32_t address, uint32_t width, uint32_t *value) {
    uint8_t *bytes;
    if (!reach(vm, address, width, PROT_READ, &bytes)) {
        return 0;
    }
    *value = width == 4 ? read_le32(bytes) : width == 2 ? read_le16(bytes) : bytes[0];
    return 1;
}

// Pushes a value; returns 0 if pushing it would fault
INLINE int push(VM *vm, uint32_t value) {
    uint32_t sp = vm->registers[R2_SP];
    uint8_t *slot = vm->memory + sp - 4;
    if (sp - 4 - vm->control[CR_SLO] >= vm->stack_span && !(slot = push_slot(vm, sp))) {
        return 0;
    }
    write_le32(slot, value);
    vm->registers[R2_SP] = sp - 4;
    return 1;
}

// Finds the word a pop at sp reads; returns 0 if popping would fault
INLINE int pop(VM *vm, uint32_t sp, uint8_t **slot) {
    *slot = vm->memory + sp;
    return sp - vm->control[CR_SLO] < vm->stack_span || (*slot = pop_slot(vm, sp)) != NULL;
}

// Pushes the address after a call and notes the call for backtraces; returns 0 if pushing would fault
INLINE int call(VM *vm, const Decoded *code, const Decoded *d) {
    uint32_t site = (uint32_t)(d - code) * 4, back = site + d->len * 4u;
    if (!push(vm, back)) {
        return 0;
    }
    cpu_push_frame(vm, site, back, -1);
    return 1;
}

// How the flags follow from what the instructions that set them kept: they are in SR; they are those of
// a + b, which a logic result keeps as a + 0, or of a - b; or Z and N are those of a, the C and O flags that
// the high half of b names are in its low half, and the others are those of the sum or difference in source,
// sa and sb, which the instructions since did not change
enum { FLAGS_SET, FLAGS_ADD, FLAGS_SUB, FLAGS_NZ };

typedef struct {
    uint32_t kind, a, b;
    uint32_t source, sa, sb;
} Flags;

// The C and O flags of a + b or a - b, or those in SR
INLINE uint32_t carry_over(uint32_t sr, uint32_t kind, uint32_t a, uint32_t b) {
    uint32_t result;
    switch (kind) {
        case FLAGS_ADD:
            result = a + b;
            return (result < a ? CARRY_FLAG : 0) | (((a ^ result) & (b ^ result)) >> 31 ? OVER_FLAG : 0);
        case FLAGS_SUB:
            result = a - b;
            return (a < b ? CARRY_FLAG : 0) | (((a ^ b) & (a ^ result)) >> 31 ? OVER_FLAG : 0);
        default:
            return sr & (CARRY_FLAG | OVER_FLAG);
    }
}

INLINE uint32_t flag_bits(uint32_t sr, const Flags *p) {
    uint32_t known;
    switch (p->kind) {
        case FLAGS_SET:
            return sr & ALU_FLAGS;
        case FLAGS_ADD:
            return zero_negative(p->a + p->b) | carry_over(sr, FLAGS_ADD, p->a, p->b);
        case FLAGS_SUB:
            return zero_negative(p->a - p->b) | carry_over(sr, FLAGS_SUB, p->a, p->b);
        default:
            known = p->b >> 16;
            return zero_negative(p->a) | (p->b & 0xFFFF) |
                   (known == (CARRY_FLAG | OVER_FLAG) ? 0 : carry_over(sr, p->source, p->sa, p->sb) & ~known);
    }
}

// Puts the flags in SR for what reads them there
INLINE void settle(uint32_t *r, Flags *p) {
    if (p->kind != FLAGS_SET) {
        r[R4_SR] = (r[R4_SR] & ~(uint32_t)ALU_FLAGS) | flag_bits(r[R4_SR], p);
        p->kind = FLAGS_SET;
    }
}

INLINE int holds(const uint32_t *r, uint16_t cond, const Flags *p) {
    return (cond >> flag_bits(r[R4_SR], p)) & 1;
}

// Whether a jump jumps. After a compare or sum, most conditions follow from whether the result is 0, whether
// it carried and whether it is less than 0 in signed terms.
INLINE int jumps(const uint32_t *r, const Decoded *d, const Flags *p) {
    if (d->cond == 0xFFFF) {
        return 1;
    }
    uint32_t quick = quick_conditions[d->op], index;
    if (p->kind == FLAGS_SUB && quick) {
        index = (p->a == p->b) | (p->a < p->b) << 1 | ((int32_t)p->a < (int32_t)p->b) << 2;
        return (quick >> index) & 1;
    }
    if (p->kind == FLAGS_ADD && quick) {
        uint32_t sum = p->a + p->b;
        index = (sum == 0) | (sum < p->a) << 1 | ((int64_t)(int32_t)p->a + (int32_t)p->b < 0) << 2;
        return (quick >> index) & 1;
    }
    return holds(r, d->cond, p);
}

// An instruction that sets Z and N from its result and C and O as f says. The C and O flags that it leaves
// alone come from the sum or difference before, which it keeps unless one before it already did.
INLINE uint32_t partial(uint32_t result, const FlagUpdate *f, Flags *p) {
    uint32_t changed = f->changed, bits = f->bits;
    if (p->kind == FLAGS_NZ) {
        p->b = ((p->b >> 16) | changed) << 16 | (p->b & 0xFFFF & ~changed) | bits;
    } else {
        p->source = p->kind;
        p->sa = p->a;
        p->sb = p->b;
        p->b = changed << 16 | bits;
        p->kind = FLAGS_NZ;
    }
    p->a = result;
    return result;
}

// A binary instruction on a register and a value: AND, OR and XOR keep their result for the flags, and ADDC
// and SUBC add the carry
INLINE uint32_t binary(uint8_t opcode, const uint32_t *r, uint32_t x, uint32_t y, Flags *p) {
    FlagUpdate f;
    if (opcode == AND_OP || opcode == OR_OP || opcode == XOR_OP) {
        p->a = opcode == AND_OP ? x & y : opcode == OR_OP ? x | y : x ^ y;
        p->b = 0;
        p->kind = FLAGS_ADD;
        return p->a;
    }
    uint32_t flags = opcode == ADDC_OP || opcode == SUBC_OP ? flag_bits(r[R4_SR], p) : 0;
    return partial(alu_result(opcode, x, y, flags, &f), &f, p);
}

#ifdef __GNUC__
// The instructions go to the code for the next through a table of labels, which spares the range check and
// the offset arithmetic of the switch
#define TARGET(kind) case kind: run_##kind
#define DISPATCH() goto *targets[d->kind]
#else
#define TARGET(kind) case kind
#define DISPATCH() goto dispatch
#endif

// Goes on with the instruction at next, or stops there once limit instructions ran. It finds the instruction
// after that one before running it, so that the host loads it while the instruction runs.
#define NEXT()             \
    do {                   \
        d = next;          \
        if (--left == 0) { \
            goto stop;     \
        }                  \
        next = d + d->len; \
        DISPATCH();        \
    } while (0)

// Runs up to limit instructions and returns how many it ran. The caller makes sure that nothing has to
// happen between them: no paging, trap, deliverable interrupt or device that counts instructions.
uint32_t cpu_run(VM *vm, uint32_t limit) {
    static int prepared;
    uint32_t *r = vm->registers;
    uint32_t left = limit, words = fetch_words(vm), index = word_index(r[R3_PC]), value;
    Flags flags = { FLAGS_SET, 0, 0, FLAGS_SET, 0, 0 };
    FlagUpdate f;

    if (!prepared) {
        prepare();
        prepared = 1;
    }
    if (!vm->decoded || vm->decoded_words != words) {
        cpu_forget_all(vm);
        vm->decoded = calloc((size_t)words + 2, sizeof(Decoded));
        if (!vm->decoded) {
            return 0;
        }
        vm->decoded_words = words;
        vm->decoded_end = (words + 1) * 4;
    }
    if (limit == 0 || index >= words) {
        return 0;
    }
#ifdef __GNUC__
    static void *const targets[] = {
        [D_DECODE] = &&run_D_DECODE,         [D_STOP] = &&run_D_STOP,             [D_NOP] = &&run_D_NOP,
        [D_LOAD_V] = &&run_D_LOAD_V,         [D_LOAD_M] = &&run_D_LOAD_M,         [D_LOADB_V] = &&run_D_LOADB_V,
        [D_LOADB_M] = &&run_D_LOADB_M,       [D_LOADW_V] = &&run_D_LOADW_V,       [D_LOADW_M] = &&run_D_LOADW_M,
        [D_STORE] = &&run_D_STORE,           [D_STOREB] = &&run_D_STOREB,         [D_STOREW] = &&run_D_STOREW,
        [D_LEA] = &&run_D_LEA,               [D_ADD_V] = &&run_D_ADD_V,           [D_ADD_M] = &&run_D_ADD_M,
        [D_SUB_V] = &&run_D_SUB_V,           [D_SUB_M] = &&run_D_SUB_M,           [D_CMP_V] = &&run_D_CMP_V,
        [D_CMP_M] = &&run_D_CMP_M,           [D_TEST_V] = &&run_D_TEST_V,         [D_TEST_M] = &&run_D_TEST_M,
        [D_BINARY_V] = &&run_D_BINARY_V,     [D_BINARY_M] = &&run_D_BINARY_M,     [D_DIVIDE_V] = &&run_D_DIVIDE_V,
        [D_DIVIDE_M] = &&run_D_DIVIDE_M,     [D_COUNT_V] = &&run_D_COUNT_V,       [D_COUNT_M] = &&run_D_COUNT_M,
        [D_FLOAT_V] = &&run_D_FLOAT_V,       [D_FLOAT_M] = &&run_D_FLOAT_M,       [D_MUL] = &&run_D_MUL,
        [D_AND] = &&run_D_AND,               [D_OR] = &&run_D_OR,                 [D_XOR] = &&run_D_XOR,
        [D_SHL] = &&run_D_SHL,               [D_SHR] = &&run_D_SHR,               [D_SAR] = &&run_D_SAR,
        [D_UNARY] = &&run_D_UNARY,           [D_FLOAT_SIGN] = &&run_D_FLOAT_SIGN, [D_SET] = &&run_D_SET,
        [D_GOTO] = &&run_D_GOTO,             [D_JZ] = &&run_D_JZ,                 [D_JNZ] = &&run_D_JNZ,
        [D_JC] = &&run_D_JC,                 [D_JAE] = &&run_D_JAE,               [D_JBE] = &&run_D_JBE,
        [D_JA] = &&run_D_JA,                 [D_JL] = &&run_D_JL,                 [D_JGE] = &&run_D_JGE,
        [D_JLE] = &&run_D_JLE,               [D_JG] = &&run_D_JG,
        [D_JUMP] = &&run_D_JUMP,             [D_JUMP_OUT] = &&run_D_JUMP_OUT,     [D_JUMP_V] = &&run_D_JUMP_V,
        [D_JUMP_M] = &&run_D_JUMP_M,         [D_CALL] = &&run_D_CALL,             [D_CALL_OUT] = &&run_D_CALL_OUT,
        [D_CALL_V] = &&run_D_CALL_V,         [D_CALL_M] = &&run_D_CALL_M,         [D_RET] = &&run_D_RET,
        [D_LOOP] = &&run_D_LOOP,             [D_LOOP_OUT] = &&run_D_LOOP_OUT,     [D_PUSH_V] = &&run_D_PUSH_V,
        [D_PUSH_M] = &&run_D_PUSH_M,         [D_POP] = &&run_D_POP,               [D_ENTER] = &&run_D_ENTER,
        [D_LEAVE] = &&run_D_LEAVE,           [D_PUSHM] = &&run_D_PUSHM,           [D_POPM] = &&run_D_POPM,
        [D_MEMCPY] = &&run_D_MEMCPY,         [D_MEMSET] = &&run_D_MEMSET,         [D_SYSCALL] = &&run_D_SYSCALL,
    };
#endif
    Decoded *code = vm->decoded, *d = code + index, *next = d + d->len;
    uint8_t *slot;
    vm->stack_span = stack_span(vm);
dispatch:
    switch (d->kind) {
        TARGET(D_DECODE):
            decode(vm, d, (uint32_t)(d - code), words);
            next = d + d->len;
            goto dispatch;
        TARGET(D_STOP):
        default:
            goto stop;
        TARGET(D_NOP):
            NEXT();
        TARGET(D_LOAD_V):
            r[d->a] = operand(r, d);
            NEXT();
        TARGET(D_LOAD_M):
            if (!load(vm, operand(r, d), 4, &r[d->a])) {
                goto stop;
            }
            NEXT();
        TARGET(D_LOADB_V):
            r[d->a] = operand(r, d) & 0xFF;
            NEXT();
        TARGET(D_LOADB_M):
            if (!load(vm, operand(r, d), 1, &r[d->a])) {
                goto stop;
            }
            NEXT();
        TARGET(D_LOADW_V):
            r[d->a] = operand(r, d) & 0xFFFF;
            NEXT();
        TARGET(D_LOADW_M):
            if (!load(vm, operand(r, d), 2, &r[d->a])) {
                goto stop;
            }
            NEXT();
        TARGET(D_STORE):
            value = operand(r, d);
            if (!reach(vm, value, 4, PROT_WRITE, &slot)) {
                goto stop;
            }
            write_le32(slot, r[d->a]);
            written(vm, value, 4);
            NEXT();
        TARGET(D_STOREB):
            value = operand(r, d);
            if (!reach(vm, value, 1, PROT_WRITE, &slot)) {
                goto stop;
            }
            slot[0] = (uint8_t)r[d->a];
            written(vm, value, 1);
            NEXT();
        TARGET(D_STOREW):
            value = operand(r, d);
            if (!reach(vm, value, 2, PROT_WRITE, &slot)) {
                goto stop;
            }
            write_le16(slot, (uint16_t)r[d->a]);
            written(vm, value, 2);
            NEXT();
        TARGET(D_LEA):
            r[d->a] = operand(r, d);
            NEXT();
        TARGET(D_ADD_V):
            value = operand(r, d);
            goto add;
        TARGET(D_ADD_M):
            if (!load(vm, operand(r, d), 4, &value)) {
                goto stop;
            }
        add:
            flags.a = r[d->a];
            flags.b = value;
            flags.kind = FLAGS_ADD;
            r[d->a] = flags.a + flags.b;
            NEXT();
        TARGET(D_SUB_V):
            value = operand(r, d);
            goto sub;
        TARGET(D_SUB_M):
            if (!load(vm, operand(r, d), 4, &value)) {
                goto stop;
            }
        sub:
            flags.a = r[d->a];
            flags.b = value;
            flags.kind = FLAGS_SUB;
            r[d->a] = flags.a - flags.b;
            NEXT();
        TARGET(D_CMP_V):
            flags.b = operand(r, d);
            flags.a = r[d->a];
            flags.kind = FLAGS_SUB;
            NEXT();
        TARGET(D_CMP_M):
            if (!load(vm, operand(r, d), 4, &value)) {
                goto stop;
            }
            flags.a = r[d->a];
            flags.b = value;
            flags.kind = FLAGS_SUB;
            NEXT();
        TARGET(D_TEST_V):
            flags.a = r[d->a] & operand(r, d);
            flags.b = 0;
            flags.kind = FLAGS_ADD;
            NEXT();
        TARGET(D_TEST_M):
            if (!load(vm, operand(r, d), 4, &value)) {
                goto stop;
            }
            flags.a = r[d->a] & value;
            flags.b = 0;
            flags.kind = FLAGS_ADD;
            NEXT();
        TARGET(D_BINARY_V):
            r[d->a] = binary(d->op, r, r[d->a], operand(r, d), &flags);
            NEXT();
        TARGET(D_BINARY_M):
            if (!load(vm, operand(r, d), 4, &value)) {
                goto stop;
            }
            r[d->a] = binary(d->op, r, r[d->a], value, &flags);
            NEXT();
        TARGET(D_DIVIDE_V):
            value = operand(r, d);
            if (!alu_divides(d->op, r[d->a], value)) {
                goto stop;
            }
            value = alu_divide(d->op, r[d->a], value, &f);
            r[d->a] = partial(value, &f, &flags);
            NEXT();
        TARGET(D_DIVIDE_M):
            if (!load(vm, operand(r, d), 4, &value) || !alu_divides(d->op, r[d->a], value)) {
                goto stop;
            }
            value = alu_divide(d->op, r[d->a], value, &f);
            r[d->a] = partial(value, &f, &flags);
            NEXT();
        TARGET(D_MUL):
            value = alu_mul(r[d->a], operand(r, d), &f);
            r[d->a] = partial(value, &f, &flags);
            NEXT();
        TARGET(D_AND):
            flags.a = r[d->a] & operand(r, d);
            goto logic;
        TARGET(D_OR):
            flags.a = r[d->a] | operand(r, d);
            goto logic;
        TARGET(D_XOR):
            flags.a = r[d->a] ^ operand(r, d);
        logic:
            flags.b = 0;
            flags.kind = FLAGS_ADD;
            r[d->a] = flags.a;
            NEXT();
        TARGET(D_SHL):
            value = alu_shl(r[d->a], operand(r, d), &f);
            r[d->a] = partial(value, &f, &flags);
            NEXT();
        TARGET(D_SHR):
            value = alu_shr(r[d->a], operand(r, d), &f);
            r[d->a] = partial(value, &f, &flags);
            NEXT();
        TARGET(D_SAR):
            value = alu_sar(r[d->a], operand(r, d), &f);
            r[d->a] = partial(value, &f, &flags);
            NEXT();
        // COUNT and the float instructions set all four flags in SR
        TARGET(D_COUNT_V):
            flags.kind = FLAGS_SET;
            r[d->a] = alu_count(d->op, &r[R4_SR], operand(r, d));
            NEXT();
        TARGET(D_COUNT_M):
            if (!load(vm, operand(r, d), 4, &value)) {
                goto stop;
            }
            flags.kind = FLAGS_SET;
            r[d->a] = alu_count(d->op, &r[R4_SR], value);
            NEXT();
        TARGET(D_FLOAT_V):
            flags.kind = FLAGS_SET;
            alu_float(d->op, &r[R4_SR], &r[d->a], operand(r, d));
            NEXT();
        TARGET(D_FLOAT_M):
            if (!load(vm, operand(r, d), 4, &value)) {
                goto stop;
            }
            flags.kind = FLAGS_SET;
            alu_float(d->op, &r[R4_SR], &r[d->a], value);
            NEXT();
        TARGET(D_UNARY):
            value = alu_unary_result(d->op, r[d->a], &f);
            r[d->a] = partial(value, &f, &flags);
            NEXT();
        TARGET(D_FLOAT_SIGN):
            flags.kind = FLAGS_SET;
            alu_float(d->op, &r[R4_SR], &r[d->a], 0);
            NEXT();
        TARGET(D_SET):
            r[d->a] = holds(r, d->cond, &flags);
            NEXT();
        TARGET(D_JUMP):
            if (jumps(r, d, &flags)) {
                next = code + d->imm;
            }
            NEXT();
        TARGET(D_GOTO):
            next = code + d->imm;
            NEXT();
        // A jump after a compare decides its condition from what was compared, and after TEST or a sum Z from
        // the sum
#define JUMP_IF(jump, compared, summed)                                                                         \
        TARGET(jump):                                                                                             \
            if (flags.kind == FLAGS_SUB ? (compared) : flags.kind == FLAGS_ADD ? (summed) : jumps(r, d, &flags)) { \
                next = code + d->imm;                                                                             \
            }                                                                                                     \
            NEXT();
        JUMP_IF(D_JZ, flags.a == flags.b, flags.a + flags.b == 0)
        JUMP_IF(D_JNZ, flags.a != flags.b, flags.a + flags.b != 0)
        JUMP_IF(D_JC, flags.a < flags.b, jumps(r, d, &flags))
        JUMP_IF(D_JAE, flags.a >= flags.b, jumps(r, d, &flags))
        JUMP_IF(D_JBE, flags.a <= flags.b, jumps(r, d, &flags))
        JUMP_IF(D_JA, flags.a > flags.b, jumps(r, d, &flags))
        JUMP_IF(D_JL, (int32_t)flags.a < (int32_t)flags.b, jumps(r, d, &flags))
        JUMP_IF(D_JGE, (int32_t)flags.a >= (int32_t)flags.b, jumps(r, d, &flags))
        JUMP_IF(D_JLE, (int32_t)flags.a <= (int32_t)flags.b, jumps(r, d, &flags))
        JUMP_IF(D_JG, (int32_t)flags.a > (int32_t)flags.b, jumps(r, d, &flags))
#undef JUMP_IF
        TARGET(D_JUMP_OUT):
            if (jumps(r, d, &flags)) {
                value = d->imm;
                goto leave;
            }
            NEXT();
        TARGET(D_JUMP_V):
            value = operand(r, d);
            if (jumps(r, d, &flags)) {
                goto go;
            }
            NEXT();
        TARGET(D_JUMP_M):
            if (!load(vm, operand(r, d), 4, &value)) {
                goto stop;
            }
            if (jumps(r, d, &flags)) {
                goto go;
            }
            NEXT();
        TARGET(D_CALL):
            if (!call(vm, code, d)) {
                goto stop;
            }
            next = code + d->imm;
            NEXT();
        TARGET(D_CALL_OUT):
            if (!call(vm, code, d)) {
                goto stop;
            }
            value = d->imm;
            goto leave;
        TARGET(D_CALL_V):
            value = operand(r, d);
            if (!call(vm, code, d)) {
                goto stop;
            }
            goto go;
        TARGET(D_CALL_M):
            if (!load(vm, operand(r, d), 4, &value) || !call(vm, code, d)) {
                goto stop;
            }
            goto go;
        TARGET(D_RET):
            if (!pop(vm, r[R2_SP], &slot)) {
                goto stop;
            }
            value = read_le32(slot);
            r[R2_SP] += 4 + d->imm;
            cpu_pop_frames(vm, 0);
            goto go;
        TARGET(D_LOOP):
            if (--r[d->a] != 0) {
                next = code + d->imm;
            }
            NEXT();
        TARGET(D_LOOP_OUT):
            if (--r[d->a] != 0) {
                value = d->imm;
                goto leave;
            }
            NEXT();
        TARGET(D_PUSH_V):
            if (!push(vm, operand(r, d))) {
                goto stop;
            }
            NEXT();
        TARGET(D_PUSH_M):
            if (!load(vm, operand(r, d), 4, &value) || !push(vm, value)) {
                goto stop;
            }
            NEXT();
        TARGET(D_POP):
            if (!pop(vm, r[R2_SP], &slot)) {
                goto stop;
            }
            r[R2_SP] += 4;
            r[d->a] = read_le32(slot);
            NEXT();
        TARGET(D_ENTER): {
            uint32_t sp = r[R2_SP];
            slot = vm->memory + sp - 4;
            uint32_t room = sp - 4 - vm->control[CR_SLO];
            if (d->imm > room || (room >= vm->stack_span && !(slot = push_slot(vm, sp)))) {
                goto stop;
            }
            write_le32(slot, r[R1_BP]);
            r[R1_BP] = sp - 4;
            r[R2_SP] = sp - 4 - d->imm;
            NEXT();
        }
        TARGET(D_LEAVE):
            if (!pop(vm, r[R1_BP], &slot)) {
                goto stop;
            }
            r[R2_SP] = r[R1_BP] + 4;
            r[R1_BP] = read_le32(slot);
            NEXT();
        TARGET(D_PUSHM): {
            // Ra to Rb go to consecutive words from the new SP up; a stack that needs other checks stops
            uint32_t sp = r[R2_SP], base = sp - d->imm;
            const uint32_t *from = r + d->a;
            if (base - vm->control[CR_SLO] >= vm->stack_span || sp > vm->control[CR_SHI]) {
                goto stop;
            }
            slot = vm->memory + base;
            switch (d->imm / 4) {
                case 11:
                    write_le32(slot + 40, from[10]);
                    // fall through
                case 10:
                    write_le32(slot + 36, from[9]);
                    // fall through
                case 9:
                    write_le32(slot + 32, from[8]);
                    // fall through
                case 8:
                    write_le32(slot + 28, from[7]);
                    // fall through
                case 7:
                    write_le32(slot + 24, from[6]);
                    // fall through
                case 6:
                    write_le32(slot + 20, from[5]);
                    // fall through
                case 5:
                    write_le32(slot + 16, from[4]);
                    // fall through
                case 4:
                    write_le32(slot + 12, from[3]);
                    // fall through
                case 3:
                    write_le32(slot + 8, from[2]);
                    // fall through
                case 2:
                    write_le32(slot + 4, from[1]);
                    // fall through
                case 1:
                    write_le32(slot + 0, from[0]);
                    // fall through
                default:
                    break;
            }
            r[R2_SP] = base;
            NEXT();
        }
        TARGET(D_POPM): {
            uint32_t sp = r[R2_SP];
            uint32_t *to = r + d->a;
            if (sp - vm->control[CR_SLO] >= vm->stack_span || sp + d->imm > vm->control[CR_SHI]) {
                goto stop;
            }
            slot = vm->memory + sp;
            switch (d->imm / 4) {
                case 11:
                    to[10] = read_le32(slot + 40);
                    // fall through
                case 10:
                    to[9] = read_le32(slot + 36);
                    // fall through
                case 9:
                    to[8] = read_le32(slot + 32);
                    // fall through
                case 8:
                    to[7] = read_le32(slot + 28);
                    // fall through
                case 7:
                    to[6] = read_le32(slot + 24);
                    // fall through
                case 6:
                    to[5] = read_le32(slot + 20);
                    // fall through
                case 5:
                    to[4] = read_le32(slot + 16);
                    // fall through
                case 4:
                    to[3] = read_le32(slot + 12);
                    // fall through
                case 3:
                    to[2] = read_le32(slot + 8);
                    // fall through
                case 2:
                    to[1] = read_le32(slot + 4);
                    // fall through
                case 1:
                    to[0] = read_le32(slot + 0);
                    // fall through
                default:
                    break;
            }
            r[R2_SP] = sp + d->imm;
            NEXT();
        }
        TARGET(D_MEMCPY):
        TARGET(D_MEMSET): {
            uint32_t size = (r[d->op] & d->mask) + d->imm, from = r[d->b], to = r[d->a];
            uint8_t *source = NULL;
            if (size == 0) {
                NEXT();
            }
            if (!reach(vm, to, size, PROT_WRITE, &slot) ||
                (d->kind == D_MEMCPY && !reach(vm, from, size, PROT_READ, &source))) {
                goto stop;
            }
            if (d->kind == D_MEMCPY) {
                memmove(slot, source, size);
            } else {
                memset(slot, (uint8_t)from, size);
            }
            written(vm, to, size);
            NEXT();
        }
        TARGET(D_SYSCALL):
            // The heap and copy syscalls of supervisor mode, which report errors in R5 instead of faulting
            if (!(r[R4_SR] & SYS_FLAG) || d->imm < SYS_ALLOC || d->imm > SYS_MEMCPY) {
                goto stop;
            }
            vm->error_pc = (uint32_t)(d - code) * 4;
            r[R3_PC] = vm->error_pc + d->len * 4u;
            settle(r, &flags);
            syscall_dispatch(vm, d->imm);
            NEXT();
    }
go:
    // A jump to a computed address
    index = word_index(value);
    if (index >= words) {
        goto leave;
    }
    next = code + index;
    NEXT();
stop:
    r[R3_PC] = (uint32_t)(d - code) * 4;
    settle(r, &flags);
    vm->instruction_count += limit - left;
    return limit - left;
leave:
    // The instruction ran and goes where cpu_run does not
    left--;
    r[R3_PC] = value;
    settle(r, &flags);
    vm->instruction_count += limit - left;
    return limit - left;
}
