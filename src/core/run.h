#ifndef _RUN_H_
#define _RUN_H_

#include <stdint.h>
#include "alu.h"
#include "vm_types.h"

// The helpers become part of each instruction they serve, where the field and width they get are constants
#ifdef __GNUC__
#define INLINE static inline __attribute__((always_inline))
#define OUTLINE static __attribute__((noinline))
#else
#define INLINE static inline
#define OUTLINE static
#endif

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

#endif // _RUN_H_
