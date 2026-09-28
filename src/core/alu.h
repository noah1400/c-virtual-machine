#ifndef _ALU_H_
#define _ALU_H_

#include <math.h>
#include <stdint.h>
#include <string.h>
#include "instruction_set.h"

// What the arithmetic, logic and float instructions compute and the flags they leave in the status
// register sr, for vm_step and cpu_run alike

#define ALU_FLAGS (ZERO_FLAG | NEG_FLAG | CARRY_FLAG | OVER_FLAG)

static inline void set_flags(uint32_t *sr, uint32_t changed, uint32_t flags) {
    *sr = (*sr & ~changed) | flags;
}

static inline uint32_t zero_negative(uint32_t result) {
    return (result == 0 ? ZERO_FLAG : 0) | (result >> 31 ? NEG_FLAG : 0);
}

static inline uint32_t alu_add(uint32_t *sr, uint32_t a, uint32_t b, uint32_t carry) {
    uint64_t wide = (uint64_t)a + b + carry;
    uint32_t result = (uint32_t)wide;
    set_flags(sr, ALU_FLAGS, zero_negative(result) | (wide >> 32 ? CARRY_FLAG : 0) |
                                 (((a ^ result) & (b ^ result)) >> 31 ? OVER_FLAG : 0));
    return result;
}

static inline uint32_t alu_sub(uint32_t *sr, uint32_t a, uint32_t b, uint32_t borrow) {
    uint32_t result = a - b - borrow;
    set_flags(sr, ALU_FLAGS, zero_negative(result) | ((uint64_t)a < (uint64_t)b + borrow ? CARRY_FLAG : 0) |
                                 (((a ^ b) & (a ^ result)) >> 31 ? OVER_FLAG : 0));
    return result;
}

// Whether DIV, MOD, IDIV or IMOD can divide a by b instead of faulting
static inline int alu_divides(uint8_t opcode, uint32_t a, uint32_t b) {
    return b != 0 && (opcode == DIV_OP || opcode == MOD_OP || a != 0x80000000u || b != 0xFFFFFFFFu);
}

static inline uint32_t alu_mul(uint32_t *sr, uint32_t a, uint32_t b) {
    uint32_t result = a * b;
    set_flags(sr, ZERO_FLAG | NEG_FLAG | OVER_FLAG, zero_negative(result) | (((uint64_t)a * b) >> 32 ? OVER_FLAG : 0));
    return result;
}

// AND, OR, XOR and TEST leave no carry or overflow behind
static inline uint32_t alu_logic(uint32_t *sr, uint32_t result) {
    set_flags(sr, ALU_FLAGS, zero_negative(result));
    return result;
}

// Shifts and rotations set C to the last bit that went out when the count is not zero
static inline uint32_t alu_shift(uint32_t *sr, uint32_t result, uint32_t count, uint32_t carry) {
    set_flags(sr, (count ? CARRY_FLAG : 0) | ZERO_FLAG | NEG_FLAG, (carry ? CARRY_FLAG : 0) | zero_negative(result));
    return result;
}

static inline uint32_t alu_shl(uint32_t *sr, uint32_t a, uint32_t b) {
    uint32_t count = b & 0x1F;
    return alu_shift(sr, a << count, count, count && (a >> (32 - count)) & 1);
}

static inline uint32_t alu_shr(uint32_t *sr, uint32_t a, uint32_t b) {
    uint32_t count = b & 0x1F;
    return alu_shift(sr, a >> count, count, count && (a >> (count - 1)) & 1);
}

static inline uint32_t alu_sar(uint32_t *sr, uint32_t a, uint32_t b) {
    uint32_t count = b & 0x1F;
    uint32_t result = (a & 0x80000000) && count ? (a >> count) | (0xFFFFFFFFu << (32 - count)) : a >> count;
    return alu_shift(sr, result, count, count && (a >> (count - 1)) & 1);
}

// DIV, MOD, IDIV and IMOD, once alu_divides allowed them
static inline uint32_t alu_divide(uint8_t opcode, uint32_t *sr, uint32_t a, uint32_t b) {
    uint32_t result = opcode == DIV_OP    ? a / b
                      : opcode == MOD_OP  ? a % b
                      : opcode == IDIV_OP ? (uint32_t)((int32_t)a / (int32_t)b)
                                          : (uint32_t)((int32_t)a % (int32_t)b);
    set_flags(sr, ZERO_FLAG | NEG_FLAG, zero_negative(result));
    return result;
}

// The result of an instruction with a register and an operand; CMP and TEST only set the flags
static inline uint32_t alu_binary(uint8_t opcode, uint32_t *sr, uint32_t a, uint32_t b) {
    uint32_t count = b & 0x1F, result;

    switch (opcode) {
        case ADD_OP:
            return alu_add(sr, a, b, 0);
        case ADDC_OP:
            return alu_add(sr, a, b, (*sr & CARRY_FLAG) != 0);
        case SUB_OP:
        case CMP_OP:
            return alu_sub(sr, a, b, 0);
        case SUBC_OP:
            return alu_sub(sr, a, b, (*sr & CARRY_FLAG) != 0);
        case MUL_OP:
            return alu_mul(sr, a, b);
        case DIV_OP:
        case MOD_OP:
        case IDIV_OP:
        case IMOD_OP:
            return alu_divide(opcode, sr, a, b);
        case MULH_OP:
            result = (uint32_t)(((int64_t)(int32_t)a * (int32_t)b) >> 32);
            break;
        case UMULH_OP:
            result = (uint32_t)(((uint64_t)a * b) >> 32);
            break;
        case AND_OP:
        case TEST_OP:
            return alu_logic(sr, a & b);
        case OR_OP:
            return alu_logic(sr, a | b);
        case XOR_OP:
            return alu_logic(sr, a ^ b);
        case SHL_OP:
            return alu_shl(sr, a, b);
        case SHR_OP:
            return alu_shr(sr, a, b);
        case SAR_OP:
            return alu_sar(sr, a, b);
        case ROL_OP:
            result = count ? (a << count) | (a >> (32 - count)) : a;
            return alu_shift(sr, result, count, result & 1);
        default:
            result = count ? (a >> count) | (a << (32 - count)) : a;
            return alu_shift(sr, result, count, result >> 31);
    }
    set_flags(sr, ZERO_FLAG | NEG_FLAG, zero_negative(result));
    return result;
}

// INC, DEC, NEG, NOT and BSWAP
static inline uint32_t alu_unary(uint8_t opcode, uint32_t *sr, uint32_t a) {
    uint32_t result;

    switch (opcode) {
        case BSWAP_OP:
            result = (a >> 24) | ((a >> 8) & 0xFF00) | ((a << 8) & 0xFF0000) | (a << 24);
            break;
        case INC_OP:
            result = a + 1;
            set_flags(sr, OVER_FLAG, a == 0x7FFFFFFF ? OVER_FLAG : 0);
            break;
        case DEC_OP:
            result = a - 1;
            set_flags(sr, OVER_FLAG, a == 0x80000000 ? OVER_FLAG : 0);
            break;
        case NEG_OP:
            result = 0u - a;
            set_flags(sr, OVER_FLAG, a == 0x80000000 ? OVER_FLAG : 0);
            break;
        default:
            result = ~a;
            break;
    }
    set_flags(sr, ZERO_FLAG | NEG_FLAG, zero_negative(result));
    return result;
}

// POPCNT, CLZ and CTZ
static inline uint32_t alu_count(uint8_t opcode, uint32_t *sr, uint32_t value) {
    uint32_t count = 0;

    if (opcode == POPCNT_OP) {
        for (; value; value &= value - 1) {
            count++;
        }
    } else if (opcode == CLZ_OP) {
        for (uint32_t bit = 0x80000000u; bit && !(value & bit); bit >>= 1) {
            count++;
        }
    } else {
        for (uint32_t bit = 1; bit && !(value & bit); bit <<= 1) {
            count++;
        }
    }
    set_flags(sr, ALU_FLAGS, zero_negative(count));
    return count;
}

static inline float to_float(uint32_t bits) {
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static inline uint32_t from_float(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// Floating-point values are IEEE single-precision bit patterns in the general registers. The result goes
// to *dest before the flags, which FCMP only sets; FNEG and FABS ignore the operand.
static inline void alu_float(uint8_t opcode, uint32_t *sr, uint32_t *dest, uint32_t operand) {
    float a = to_float(*dest), b = to_float(operand), result;

    switch (opcode) {
        case FADD_OP:
            result = a + b;
            break;
        case FSUB_OP:
            result = a - b;
            break;
        case FMUL_OP:
            result = a * b;
            break;
        case FDIV_OP:
            result = a / b;
            break;
        case FSQRT_OP:
            result = sqrtf(b);
            break;
        case FNEG_OP:
            result = -a;
            break;
        case FABS_OP:
            result = fabsf(a);
            break;
        case ITOF_OP:
            result = (float)(int32_t)operand;
            break;
        case FCMP_OP: {
            // Unsigned conditions apply; unordered operands also set O
            int unordered = isnan(a) || isnan(b);
            set_flags(sr, ALU_FLAGS, (unordered || a == b ? ZERO_FLAG : 0) | (unordered || a < b ? CARRY_FLAG : 0) |
                                         (unordered ? OVER_FLAG : 0));
            return;
        }
        default: {
            // NaN and values outside the int32 range give INT32_MIN and set O
            int valid = b >= -2147483648.0f && b < 2147483648.0f;
            *dest = valid ? (uint32_t)(int32_t)b : 0x80000000u;
            set_flags(sr, ALU_FLAGS, zero_negative(*dest) | (valid ? 0 : OVER_FLAG));
            return;
        }
    }

    *dest = from_float(result);
    set_flags(sr, ALU_FLAGS, (result == 0.0f ? ZERO_FLAG : 0) | (signbit(result) && !isnan(result) ? NEG_FLAG : 0));
}

// Whether the condition of a jump or SET holds; JMP and CALL always jump
static inline int alu_condition(uint32_t sr, uint8_t opcode) {
    int zero = (sr & ZERO_FLAG) != 0, negative = (sr & NEG_FLAG) != 0, carry = (sr & CARRY_FLAG) != 0;
    int over = (sr & OVER_FLAG) != 0, less = negative != over;

    switch (opcode) {
        case JZ_OP:
            return zero;
        case JNZ_OP:
            return !zero;
        case JN_OP:
            return negative;
        case JP_OP:
            return !negative && !zero;
        case JO_OP:
            return over;
        case JC_OP:
            return carry;
        case JBE_OP:
            return carry || zero;
        case JA_OP:
            return !carry && !zero;
        case JAE_OP:
            return !carry;
        case JL_OP:
            return less;
        case JGE_OP:
            return !less;
        case JLE_OP:
            return less || zero;
        case JG_OP:
            return !less && !zero;
        default:
            return 1;
    }
}

#endif // _ALU_H_
