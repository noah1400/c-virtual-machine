#include <string.h>
#include "alu.h"
#include "binfmt.h"
#include "cpu.h"
#include "memory.h"
#include "vm.h"

// cpu_run executes the common instructions without the decoding, copies and checks that vm_step spends
// on every instruction. It stops before an instruction that would fault or that needs more than it
// does, such as a syscall, a device, a privileged instruction or a write to PC or SR as a register, and
// leaves that one to vm_step, which does the same as it would.

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
    RUN_MEMCPY,
    RUN_MEMSET,
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
    [ENTER_OP] = RUN_ENTER,   [LEAVE_OP] = RUN_LEAVE,   [MEMCPY_OP] = RUN_MEMCPY, [MEMSET_OP] = RUN_MEMSET,
};

#define EXTENDED_BIT (MODE_EXTENDED << 20)

// The helpers become part of each instruction they serve, where the field and width they get are constants
#ifdef __GNUC__
#define INLINE static inline __attribute__((always_inline))
#else
#define INLINE static inline
#endif

// The fields of the registers that hold or point at an operand
#define REG1 16
#define REG2 12

// What to do with each instruction, by the opcode, extension bit and mode in the top 12 bits of its first
// word; the instructions and modes that vm_step would refuse stop
static uint8_t kinds[4096];

// For each jump opcode, one bit for each value of the Z, N, C and O flags under which it jumps
static uint16_t conditions[256];

static void prepare(void) {
    for (uint32_t key = 0; key < 4096; key++) {
        const InstructionInfo *info = isa_by_opcode((uint8_t)(key >> 4));
        uint32_t mode = MODE_BIT(key & 7), extended = key & MODE_EXTENDED;
        if (!info || info->privileged || (info->modes && !(info->modes & mode)) ||
            (extended && !(info->modes & mode & MODES_WITH_IMMEDIATE))) {
            continue;
        }
        kinds[key] = runs[key >> 4];
    }
    for (uint32_t opcode = 0; opcode < 256; opcode++) {
        for (uint32_t flags = 0; flags < 16; flags++) {
            conditions[opcode] |= (uint16_t)(alu_condition(flags, (uint8_t)opcode) << flags);
        }
    }
}

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

// The host address of size bytes at address if accessing them needs no fault; cpu_run runs without paging
INLINE uint8_t *reach(VM *vm, uint32_t address, uint32_t size, uint8_t access) {
    uint64_t end = (uint64_t)address + size;
    if (end <= vm->memory_size && (end <= vm->control[CR_HEAPLO] || address >= vm->control[CR_HEAPHI] ||
                                   (size <= HEAP_UNIT && memory_heap_allows(vm, address, size, access)))) {
        return vm->memory + address;
    }
    return memory_direct(vm, address, size, access);
}

INLINE uint32_t operand_address(const uint32_t *r, uint32_t w, const uint8_t *code, int field) {
    int extended = (w & EXTENDED_BIT) != 0;
    switch ((w >> 20) & 7) {
        case MEM_MODE:
            return extended ? extension(code) : w & 0xFFFF;
        case REGM_MODE:
            return r[(w >> field) & 0x0F];
        case IDX_MODE:
            return r[(w >> field) & 0x0F] + (extended ? extension(code) : signed12(w));
        case STK_MODE:
            return r[R2_SP] + immediate(w, code);
        default:
            return r[R1_BP] + immediate(w, code);
    }
}

// Reads the variable operand, zero-extended from width bytes; returns 0 if reading it would fault
INLINE int read_operand(VM *vm, uint32_t w, const uint8_t *code, int field, uint32_t width, uint32_t *value) {
    uint32_t mode = (w >> 20) & 7;
    if (mode == IMM_MODE || mode == REG_MODE) {
        uint32_t v = mode == IMM_MODE ? immediate(w, code) : vm->registers[(w >> field) & 0x0F];
        *value = width == 4 ? v : v & ((1u << (8 * width)) - 1);
        return 1;
    }
    const uint8_t *bytes = reach(vm, operand_address(vm->registers, w, code, field), width, PROT_READ);
    if (!bytes) {
        return 0;
    }
    *value = width == 4 ? read_le32(bytes) : width == 2 ? read_le16(bytes) : bytes[0];
    return 1;
}

// Immediate targets are relative to the next instruction
INLINE int jump_target(VM *vm, uint32_t w, const uint8_t *code, uint32_t next, int field, uint32_t *target) {
    if (((w >> 20) & 7) == IMM_MODE) {
        *target = next + immediate(w, code);
        return 1;
    }
    return read_operand(vm, w, code, field, 4, target);
}

// MEMCPY and MEMSET take their size from the immediate or from the register it names
INLINE uint32_t block_size(const uint32_t *r, uint32_t w, const uint8_t *code) {
    if (((w >> 20) & 7) == REG_MODE) {
        return r[w & 0x0F];
    }
    return w & EXTENDED_BIT ? extension(code) : w & 0x0FFF;
}

// The stack lies between SLO and SHI
INLINE uint8_t *push_slot(VM *vm, uint32_t sp) {
    uint32_t low = vm->control[CR_SLO];
    if (sp < low || sp > vm->control[CR_SHI] || sp - low < 4) {
        return NULL;
    }
    return reach(vm, sp - 4, 4, PROT_WRITE);
}

INLINE uint8_t *pop_slot(VM *vm, uint32_t sp) {
    uint32_t high = vm->control[CR_SHI];
    if (sp < vm->control[CR_SLO] || sp > high || high - sp < 4) {
        return NULL;
    }
    return reach(vm, sp, 4, PROT_READ);
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

// Runs up to limit instructions and returns how many it ran. The caller makes sure that nothing has to
// happen between them: no paging, trap, deliverable interrupt or device that counts instructions.
uint32_t cpu_run(VM *vm, uint32_t limit) {
    static int prepared;
    uint32_t *r = vm->registers;
    uint32_t pc = r[R3_PC], left = limit, words = fetch_words(vm);

    if (!prepared) {
        prepare();
        prepared = 1;
    }
    for (; left > 0 && word_index(pc) < words; left--) {
        const uint8_t *code = vm->memory + pc;
        uint32_t w = read_le32(code), next = pc + 4 + ((w >> 21) & 4), value;
        uint8_t *slot;

        // PC already points at the next instruction while this one executes
        r[R3_PC] = next;
        switch (kinds[w >> 20]) {
            case RUN_NOP:
                break;
            case RUN_LOAD:
                if (writes_special(reg1(w)) || !read_operand(vm, w, code, REG2, 4, &value)) {
                    goto stop;
                }
                r[reg1(w)] = value;
                break;
            case RUN_LOADB:
                if (writes_special(reg1(w)) || !read_operand(vm, w, code, REG2, 1, &value)) {
                    goto stop;
                }
                r[reg1(w)] = value;
                break;
            case RUN_LOADW:
                if (writes_special(reg1(w)) || !read_operand(vm, w, code, REG2, 2, &value)) {
                    goto stop;
                }
                r[reg1(w)] = value;
                break;
            case RUN_STORE:
                if (!(slot = reach(vm, operand_address(r, w, code, REG2), 4, PROT_WRITE))) {
                    goto stop;
                }
                write_le32(slot, r[reg1(w)]);
                break;
            case RUN_STOREB:
                if (!(slot = reach(vm, operand_address(r, w, code, REG2), 1, PROT_WRITE))) {
                    goto stop;
                }
                slot[0] = (uint8_t)r[reg1(w)];
                break;
            case RUN_STOREW:
                if (!(slot = reach(vm, operand_address(r, w, code, REG2), 2, PROT_WRITE))) {
                    goto stop;
                }
                write_le16(slot, (uint16_t)r[reg1(w)]);
                break;
            case RUN_LEA:
                if (writes_special(reg1(w))) {
                    goto stop;
                }
                r[reg1(w)] = operand_address(r, w, code, REG2);
                break;
            case RUN_MOVE:
                if (writes_special(reg1(w))) {
                    goto stop;
                }
                r[reg1(w)] = r[(w >> REG2) & 0x0F];
                break;
            case RUN_ADD:
                if (writes_special(reg1(w)) || !read_operand(vm, w, code, REG2, 4, &value)) {
                    goto stop;
                }
                r[reg1(w)] = alu_add(&r[R4_SR], r[reg1(w)], value, 0);
                break;
            case RUN_SUB:
                if (writes_special(reg1(w)) || !read_operand(vm, w, code, REG2, 4, &value)) {
                    goto stop;
                }
                r[reg1(w)] = alu_sub(&r[R4_SR], r[reg1(w)], value, 0);
                break;
            case RUN_CMP:
                if (!read_operand(vm, w, code, REG2, 4, &value)) {
                    goto stop;
                }
                alu_sub(&r[R4_SR], r[reg1(w)], value, 0);
                break;
            case RUN_TEST:
                if (!read_operand(vm, w, code, REG2, 4, &value)) {
                    goto stop;
                }
                alu_binary(TEST_OP, &r[R4_SR], r[reg1(w)], value);
                break;
            case RUN_BINARY:
                if (writes_special(reg1(w)) || !read_operand(vm, w, code, REG2, 4, &value)) {
                    goto stop;
                }
                r[reg1(w)] = alu_binary((uint8_t)(w >> 24), &r[R4_SR], r[reg1(w)], value);
                break;
            case RUN_DIVIDE:
                if (writes_special(reg1(w)) || !read_operand(vm, w, code, REG2, 4, &value) ||
                    !alu_divides((uint8_t)(w >> 24), r[reg1(w)], value)) {
                    goto stop;
                }
                r[reg1(w)] = alu_binary((uint8_t)(w >> 24), &r[R4_SR], r[reg1(w)], value);
                break;
            case RUN_UNARY:
                if (writes_special(reg1(w))) {
                    goto stop;
                }
                r[reg1(w)] = alu_unary((uint8_t)(w >> 24), &r[R4_SR], r[reg1(w)]);
                break;
            case RUN_COUNT:
                if (writes_special(reg1(w)) || !read_operand(vm, w, code, REG2, 4, &value)) {
                    goto stop;
                }
                r[reg1(w)] = alu_count((uint8_t)(w >> 24), &r[R4_SR], value);
                break;
            case RUN_FLOAT:
                if (writes_special(reg1(w)) || !read_operand(vm, w, code, REG2, 4, &value)) {
                    goto stop;
                }
                alu_float((uint8_t)(w >> 24), &r[R4_SR], &r[reg1(w)], value);
                break;
            case RUN_FLOAT_SIGN:
                if (writes_special(reg1(w))) {
                    goto stop;
                }
                alu_float((uint8_t)(w >> 24), &r[R4_SR], &r[reg1(w)], 0);
                break;
            case RUN_SET:
                value = immediate(w, code);
                if (writes_special(reg1(w)) || value > 0xFF || !isa_is_conditional_jump((uint8_t)value)) {
                    goto stop;
                }
                r[reg1(w)] = (conditions[value] >> (r[R4_SR] & 0x0F)) & 1;
                break;
            case RUN_JUMP:
                if (!jump_target(vm, w, code, next, REG1, &value)) {
                    goto stop;
                }
                if ((conditions[w >> 24] >> (r[R4_SR] & 0x0F)) & 1) {
                    next = value;
                }
                break;
            case RUN_CALL:
                if (!jump_target(vm, w, code, next, REG1, &value) || !(slot = push_slot(vm, r[R2_SP]))) {
                    goto stop;
                }
                write_le32(slot, next);
                r[R2_SP] -= 4;
                cpu_push_frame(vm, pc, next, -1);
                next = value;
                break;
            case RUN_RET:
                if (!(slot = pop_slot(vm, r[R2_SP]))) {
                    goto stop;
                }
                next = read_le32(slot);
                r[R2_SP] += 4 + immediate(w, code);
                cpu_pop_frames(vm, 0);
                break;
            case RUN_LOOP:
                if (writes_special(reg1(w)) || !jump_target(vm, w, code, next, REG2, &value)) {
                    goto stop;
                }
                if (--r[reg1(w)] != 0) {
                    next = value;
                }
                break;
            case RUN_PUSH:
                if (!read_operand(vm, w, code, REG1, 4, &value) || !(slot = push_slot(vm, r[R2_SP]))) {
                    goto stop;
                }
                write_le32(slot, value);
                r[R2_SP] -= 4;
                break;
            case RUN_POP:
                if (writes_special(reg1(w)) || !(slot = pop_slot(vm, r[R2_SP]))) {
                    goto stop;
                }
                r[R2_SP] += 4;
                r[reg1(w)] = read_le32(slot);
                break;
            case RUN_ENTER: {
                uint32_t sp = r[R2_SP], size = immediate(w, code);
                if (!(slot = push_slot(vm, sp)) || size > sp - 4 - vm->control[CR_SLO]) {
                    goto stop;
                }
                write_le32(slot, r[R1_BP]);
                r[R1_BP] = sp - 4;
                r[R2_SP] = sp - 4 - size;
                break;
            }
            case RUN_LEAVE:
                if (!(slot = pop_slot(vm, r[R1_BP]))) {
                    goto stop;
                }
                r[R2_SP] = r[R1_BP] + 4;
                r[R1_BP] = read_le32(slot);
                break;
            case RUN_MEMCPY:
            case RUN_MEMSET: {
                uint32_t size = block_size(r, w, code), from = r[(w >> REG2) & 0x0F];
                const uint8_t *source = NULL;
                if (size == 0) {
                    break;
                }
                if (!(slot = reach(vm, r[reg1(w)], size, PROT_WRITE)) ||
                    (kinds[w >> 20] == RUN_MEMCPY && !(source = reach(vm, from, size, PROT_READ)))) {
                    goto stop;
                }
                if (source) {
                    memmove(slot, source, size);
                } else {
                    memset(slot, (uint8_t)from, size);
                }
                break;
            }
            default:
                goto stop;
        }
        pc = next;
    }
stop:
    r[R3_PC] = pc;
    vm->instruction_count += limit - left;
    return limit - left;
}
