#include <stdio.h>
#include "cpu.h"
#include "io.h"
#include "memory.h"
#include "syscalls.h"
#include "vm.h"

static uint32_t effective_address(VM *vm, const Instruction *instr, uint8_t reg) {
    switch (instr->mode) {
        case MEM_MODE:
            return instr->immediate;
        case REGM_MODE:
            return vm->registers[reg];
        case IDX_MODE:
            return vm->registers[reg] + instr->immediate;
        case STK_MODE:
            return vm->registers[R2_SP] + instr->immediate;
        case BAS_MODE:
            return vm->registers[R1_BP] + instr->immediate;
        default:
            vm_raise(vm, VM_ERROR_INVALID_INSTRUCTION, "Addressing mode %s has no address",
                     isa_mode_name(instr->mode));
            return 0;
    }
}

static uint32_t load(VM *vm, uint32_t address, int width) {
    switch (width) {
        case 1:
            return memory_read_byte(vm, address);
        case 2:
            return memory_read_word(vm, address);
        default:
            return memory_read_dword(vm, address);
    }
}

static void store(VM *vm, uint32_t address, uint32_t value, int width) {
    switch (width) {
        case 1:
            memory_write_byte(vm, address, (uint8_t)value);
            break;
        case 2:
            memory_write_word(vm, address, (uint16_t)value);
            break;
        default:
            memory_write_dword(vm, address, value);
            break;
    }
}

// Value of the variable operand whose base register is reg, zero-extended from width bytes
static uint32_t read_operand(VM *vm, const Instruction *instr, uint8_t reg, int width) {
    uint32_t value;

    switch (instr->mode) {
        case IMM_MODE:
            value = instr->immediate;
            break;
        case REG_MODE:
            value = vm->registers[reg];
            break;
        default:
            return load(vm, effective_address(vm, instr, reg), width);
    }
    return width == 4 ? value : value & ((1u << (8 * width)) - 1);
}

static void set_zero_negative(VM *vm, uint32_t result) {
    cpu_set_flag(vm, ZERO_FLAG, result == 0);
    cpu_set_flag(vm, NEG_FLAG, result >> 31);
}

static uint32_t add_with_flags(VM *vm, uint32_t a, uint32_t b, uint32_t carry_in) {
    uint64_t wide = (uint64_t)a + b + carry_in;
    uint32_t result = (uint32_t)wide;

    cpu_set_flag(vm, CARRY_FLAG, (uint8_t)(wide >> 32));
    cpu_set_flag(vm, OVER_FLAG, ((a ^ result) & (b ^ result)) >> 31);
    set_zero_negative(vm, result);
    return result;
}

static uint32_t sub_with_flags(VM *vm, uint32_t a, uint32_t b, uint32_t borrow_in) {
    uint32_t result = a - b - borrow_in;

    cpu_set_flag(vm, CARRY_FLAG, (uint64_t)a < (uint64_t)b + borrow_in);
    cpu_set_flag(vm, OVER_FLAG, ((a ^ b) & (a ^ result)) >> 31);
    set_zero_negative(vm, result);
    return result;
}

static int execute_arithmetic(VM *vm, const Instruction *instr) {
    uint32_t *dest = &vm->registers[instr->reg1];
    uint32_t a = *dest;
    uint32_t b = read_operand(vm, instr, instr->reg2, 4);
    uint32_t result;

    if (vm->last_error != VM_ERROR_NONE) {
        return vm->last_error;
    }

    switch (instr->opcode) {
        case ADD_OP:
            *dest = add_with_flags(vm, a, b, 0);
            break;
        case ADDC_OP:
            *dest = add_with_flags(vm, a, b, cpu_get_flag(vm, CARRY_FLAG));
            break;
        case SUB_OP:
            *dest = sub_with_flags(vm, a, b, 0);
            break;
        case SUBC_OP:
            *dest = sub_with_flags(vm, a, b, cpu_get_flag(vm, CARRY_FLAG));
            break;
        case CMP_OP:
            sub_with_flags(vm, a, b, 0);
            break;
        case MUL_OP:
            result = a * b;
            cpu_set_flag(vm, OVER_FLAG, ((uint64_t)a * b) >> 32 != 0);
            set_zero_negative(vm, result);
            *dest = result;
            break;
        case DIV_OP:
        case MOD_OP:
            if (b == 0) {
                return vm_raise(vm, VM_ERROR_DIVISION_BY_ZERO,
                                instr->opcode == DIV_OP ? "Division by zero" : "Modulo by zero");
            }
            result = instr->opcode == DIV_OP ? a / b : a % b;
            set_zero_negative(vm, result);
            *dest = result;
            break;
        case IDIV_OP:
        case IMOD_OP: {
            int32_t x = (int32_t)a, y = (int32_t)b;
            if (y == 0) {
                return vm_raise(vm, VM_ERROR_DIVISION_BY_ZERO,
                                instr->opcode == IDIV_OP ? "Division by zero" : "Modulo by zero");
            }
            if (x == INT32_MIN && y == -1) {
                return vm_raise(vm, VM_ERROR_DIVISION_BY_ZERO, "Signed division overflow");
            }
            result = (uint32_t)(instr->opcode == IDIV_OP ? x / y : x % y);
            set_zero_negative(vm, result);
            *dest = result;
            break;
        }
    }
    return VM_ERROR_NONE;
}

static int execute_unary(VM *vm, const Instruction *instr) {
    uint32_t *dest = &vm->registers[instr->reg1];
    uint32_t a = *dest;
    uint32_t result;

    switch (instr->opcode) {
        case INC_OP:
            result = a + 1;
            cpu_set_flag(vm, OVER_FLAG, a == 0x7FFFFFFF);
            break;
        case DEC_OP:
            result = a - 1;
            cpu_set_flag(vm, OVER_FLAG, a == 0x80000000);
            break;
        case NEG_OP:
            result = 0u - a;
            cpu_set_flag(vm, OVER_FLAG, a == 0x80000000);
            break;
        default:
            result = ~a;
            break;
    }

    set_zero_negative(vm, result);
    *dest = result;
    return VM_ERROR_NONE;
}

static int execute_logical(VM *vm, const Instruction *instr) {
    uint32_t *dest = &vm->registers[instr->reg1];
    uint32_t a = *dest;
    uint32_t b = read_operand(vm, instr, instr->reg2, 4);
    uint32_t count = b & 0x1F;
    uint32_t result;

    if (vm->last_error != VM_ERROR_NONE) {
        return vm->last_error;
    }

    // Bitwise operations leave no carry or overflow behind
    if (instr->opcode <= XOR_OP || instr->opcode == TEST_OP) {
        cpu_set_flag(vm, CARRY_FLAG, 0);
        cpu_set_flag(vm, OVER_FLAG, 0);
    }

    switch (instr->opcode) {
        case AND_OP:
        case TEST_OP:
            result = a & b;
            break;
        case OR_OP:
            result = a | b;
            break;
        case XOR_OP:
            result = a ^ b;
            break;
        case SHL_OP:
            if (count) {
                cpu_set_flag(vm, CARRY_FLAG, (a >> (32 - count)) & 1);
            }
            result = a << count;
            break;
        case SHR_OP:
            if (count) {
                cpu_set_flag(vm, CARRY_FLAG, (a >> (count - 1)) & 1);
            }
            result = a >> count;
            break;
        case SAR_OP:
            if (count) {
                cpu_set_flag(vm, CARRY_FLAG, (a >> (count - 1)) & 1);
            }
            result = (a & 0x80000000) && count ? (a >> count) | (0xFFFFFFFFu << (32 - count)) : a >> count;
            break;
        case ROL_OP:
            result = count ? (a << count) | (a >> (32 - count)) : a;
            if (count) {
                cpu_set_flag(vm, CARRY_FLAG, result & 1);
            }
            break;
        default:
            result = count ? (a >> count) | (a << (32 - count)) : a;
            if (count) {
                cpu_set_flag(vm, CARRY_FLAG, result >> 31);
            }
            break;
    }

    set_zero_negative(vm, result);
    if (instr->opcode != TEST_OP) {
        *dest = result;
    }
    return VM_ERROR_NONE;
}

static int branch_taken(VM *vm, uint8_t opcode) {
    int zero = cpu_get_flag(vm, ZERO_FLAG);
    int negative = cpu_get_flag(vm, NEG_FLAG);
    int carry = cpu_get_flag(vm, CARRY_FLAG);
    int less = negative != cpu_get_flag(vm, OVER_FLAG);

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
            return cpu_get_flag(vm, OVER_FLAG);
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

// Immediate targets are relative to the next instruction, which PC already points at
static uint32_t branch_target(VM *vm, const Instruction *instr, uint8_t reg) {
    return instr->mode == IMM_MODE ? vm->registers[R3_PC] + instr->immediate : read_operand(vm, instr, reg, 4);
}

static int execute_control(VM *vm, const Instruction *instr) {
    uint32_t target;

    switch (instr->opcode) {
        case RET_OP:
            vm->registers[R3_PC] = cpu_stack_pop(vm);
            vm->registers[R2_SP] += instr->immediate;
            return vm->last_error;

        case SYSCALL_OP:
            return syscall_dispatch(vm, instr->immediate);

        case LOOP_OP:
            target = branch_target(vm, instr, instr->reg2);
            if (vm->last_error == VM_ERROR_NONE && --vm->registers[instr->reg1] != 0) {
                vm->registers[R3_PC] = target;
            }
            return vm->last_error;

        default:
            target = branch_target(vm, instr, instr->reg1);
            if (vm->last_error != VM_ERROR_NONE) {
                return vm->last_error;
            }
            if (instr->opcode == CALL_OP) {
                cpu_stack_push(vm, vm->registers[R3_PC]);
            }
            if (vm->last_error == VM_ERROR_NONE && branch_taken(vm, instr->opcode)) {
                vm->registers[R3_PC] = target;
            }
            return vm->last_error;
    }
}

static int execute_stack(VM *vm, const Instruction *instr) {
    switch (instr->opcode) {
        case PUSH_OP: {
            uint32_t value = read_operand(vm, instr, instr->reg1, 4);
            if (vm->last_error == VM_ERROR_NONE) {
                cpu_stack_push(vm, value);
            }
            break;
        }
        case POP_OP: {
            uint32_t value = cpu_stack_pop(vm);
            if (vm->last_error == VM_ERROR_NONE) {
                vm->registers[instr->reg1] = value;
            }
            break;
        }
        case PUSHF_OP:
            cpu_stack_push(vm, vm->registers[R4_SR]);
            break;
        case POPF_OP: {
            uint32_t value = cpu_stack_pop(vm);
            if (vm->last_error == VM_ERROR_NONE) {
                vm->registers[R4_SR] = value;
            }
            break;
        }
        case PUSHA_OP:
            cpu_push_all(vm);
            break;
        case POPA_OP:
            cpu_pop_all(vm, 0);
            break;
        case ENTER_OP:
            cpu_enter_frame(vm, instr->immediate);
            break;
        case LEAVE_OP:
            cpu_leave_frame(vm);
            break;
    }
    return vm->last_error;
}

static void execute_cpuid(VM *vm) {
    size_t instruction_count;

    switch (vm->registers[R0_ACC]) {
        case 0:
            // Highest function number, then the vendor string "VM32CPU" in R5-R6
            vm->registers[R0_ACC] = 4;
            vm->registers[R5] = 0x32334D56;
            vm->registers[R6] = 0x00555043;
            vm->registers[R7] = 0;
            break;
        case 1:
            // Version 1.1.0; R5 features: I/O ports, memory protection, interrupts, syscalls;
            // R6 features: debug support, timer device
            vm->registers[R0_ACC] = 0x00010001;
            vm->registers[R5] = 0x00000004 | 0x00000008 | 0x00000010 | 0x00000020;
            vm->registers[R6] = 0x00000001 | 0x00000002;
            vm->registers[R7] = 0;
            break;
        case 2:
            // Memory size, segment bases / 256 and segment sizes in KB, one byte each
            vm->registers[R0_ACC] = vm->memory_size;
            vm->registers[R5] = ((CODE_SEGMENT_BASE >> 8) << 24) | ((DATA_SEGMENT_BASE >> 8) << 16) |
                                ((STACK_SEGMENT_BASE >> 8) << 8) | (HEAP_SEGMENT_BASE >> 8);
            vm->registers[R6] = ((CODE_SEGMENT_SIZE / 1024) << 24) | ((DATA_SEGMENT_SIZE / 1024) << 16) |
                                ((STACK_SEGMENT_SIZE / 1024) << 8) | (HEAP_SEGMENT_SIZE / 1024);
            vm->registers[R7] = 0;
            break;
        case 3:
            // Number of instructions, addressing mode mask, instruction group mask
            isa_table(&instruction_count);
            vm->registers[R0_ACC] = (uint32_t)instruction_count;
            vm->registers[R5] = MODES_SRC;
            vm->registers[R6] = 0x0000007F;
            vm->registers[R7] = 0;
            break;
        case 4:
            // Instruction count, state flags (bit 1: debug mode, bit 2: interrupts enabled), last error
            vm->registers[R0_ACC] = vm->instruction_count;
            vm->registers[R5] = (vm->debug_mode ? 0x02 : 0) | (cpu_get_flag(vm, INT_FLAG) ? 0x04 : 0);
            vm->registers[R6] = vm->last_error;
            vm->registers[R7] = 0;
            break;
        default:
            vm->registers[R0_ACC] = 0;
            vm->registers[R5] = 0;
            vm->registers[R6] = 0;
            vm->registers[R7] = 0;
            break;
    }
}

static int execute_system(VM *vm, const Instruction *instr) {
    switch (instr->opcode) {
        case HALT_OP:
            vm->halted = 1;
            break;
        case INT_OP:
            if (instr->immediate > 0xFF) {
                return vm_raise(vm, VM_ERROR_UNHANDLED_INTERRUPT, "Invalid interrupt vector: %u", instr->immediate);
            }
            cpu_interrupt(vm, (uint8_t)instr->immediate);
            break;
        case CLI_OP:
            cpu_disable_interrupts(vm);
            break;
        case STI_OP:
            cpu_enable_interrupts(vm);
            break;
        case IRET_OP:
            cpu_pop_all(vm, 1);
            break;
        case IN_OP: {
            uint32_t value = io_read(vm, read_operand(vm, instr, instr->reg2, 4));
            if (vm->last_error == VM_ERROR_NONE) {
                vm->registers[instr->reg1] = value;
            }
            break;
        }
        case OUT_OP:
            io_write(vm, read_operand(vm, instr, instr->reg2, 4), vm->registers[instr->reg1]);
            break;
        case CPUID_OP:
            execute_cpuid(vm);
            break;
        case RESET_OP:
            cpu_reset(vm);
            break;
        case DEBUG_OP:
            vm->break_requested = 1;
            break;
        case MFCR_OP:
        case MTCR_OP:
            if (instr->immediate >= CR_COUNT) {
                return vm_raise(vm, VM_ERROR_INVALID_INSTRUCTION, "Invalid control register %u", instr->immediate);
            }
            if (instr->opcode == MFCR_OP) {
                vm->registers[instr->reg1] = vm->control[instr->immediate];
                break;
            }
            vm->control[instr->immediate] = vm->registers[instr->reg1];
            // Moving the heap forgets every block in it
            if (instr->immediate == CR_HEAPLO || instr->immediate == CR_HEAPHI) {
                memory_heap_reset(vm);
            }
            break;
    }
    return vm->last_error;
}

static int execute_memory(VM *vm, const Instruction *instr) {
    uint32_t first = vm->registers[instr->reg1];
    uint32_t second = vm->registers[instr->reg2];

    switch (instr->opcode) {
        case ALLOC_OP: {
            uint32_t address = memory_allocate(vm, read_operand(vm, instr, instr->reg2, 4));
            if (address) {
                vm->registers[instr->reg1] = address;
            }
            break;
        }
        case FREE_OP:
            memory_free(vm, first);
            break;
        case MEMCPY_OP:
        case MEMSET_OP: {
            // The size is an immediate or a register named in the immediate field
            uint32_t size = instr->mode == REG_MODE ? vm->registers[instr->immediate & 0x0F] : instr->immediate;
            if (instr->opcode == MEMCPY_OP) {
                memory_copy(vm, first, second, size);
            } else {
                memory_set(vm, first, (uint8_t)second, size);
            }
            break;
        }
        case PROTECT_OP:
            memory_protect(vm, first, (uint8_t)read_operand(vm, instr, instr->reg2, 4));
            break;
    }
    return vm->last_error;
}

int cpu_execute_instruction(VM *vm, const Instruction *instr) {
    const InstructionInfo *info = isa_by_opcode(instr->opcode);

    vm->current_instr = *instr;

    if (!info) {
        return vm_raise(vm, VM_ERROR_INVALID_INSTRUCTION, "Invalid opcode: 0x%02X", instr->opcode);
    }
    if (info->modes && !(info->modes & MODE_BIT(instr->mode))) {
        return vm_raise(vm, VM_ERROR_INVALID_INSTRUCTION, "Invalid addressing mode %s for %s",
                        isa_mode_name(instr->mode), info->mnemonic);
    }
    if (instr->extended && !(info->modes & MODE_BIT(instr->mode) & MODES_WITH_IMMEDIATE)) {
        return vm_raise(vm, VM_ERROR_INVALID_INSTRUCTION, "%s has no immediate for an extension word",
                        info->mnemonic);
    }

    switch (instr->opcode) {
        case NOP_OP:
            return VM_ERROR_NONE;

        case LOAD_OP:
        case LOADB_OP:
        case LOADW_OP: {
            int width = instr->opcode == LOAD_OP ? 4 : instr->opcode == LOADW_OP ? 2 : 1;
            uint32_t value = read_operand(vm, instr, instr->reg2, width);
            if (vm->last_error == VM_ERROR_NONE) {
                vm->registers[instr->reg1] = value;
            }
            return vm->last_error;
        }

        case STORE_OP:
        case STOREB_OP:
        case STOREW_OP: {
            int width = instr->opcode == STORE_OP ? 4 : instr->opcode == STOREW_OP ? 2 : 1;
            uint32_t address = effective_address(vm, instr, instr->reg2);
            if (vm->last_error == VM_ERROR_NONE) {
                store(vm, address, vm->registers[instr->reg1], width);
            }
            return vm->last_error;
        }

        case LEA_OP:
            vm->registers[instr->reg1] = effective_address(vm, instr, instr->reg2);
            return vm->last_error;

        case MOVE_OP:
            vm->registers[instr->reg1] = vm->registers[instr->reg2];
            return VM_ERROR_NONE;

        case INC_OP:
        case DEC_OP:
        case NEG_OP:
        case NOT_OP:
            return execute_unary(vm, instr);

        default:
            break;
    }

    // The top three opcode bits select the instruction group
    switch (instr->opcode >> 5) {
        case 1:
            return execute_arithmetic(vm, instr);
        case 2:
            return execute_logical(vm, instr);
        case 3:
            return execute_control(vm, instr);
        case 4:
            return execute_stack(vm, instr);
        case 5:
            return execute_system(vm, instr);
        default:
            return execute_memory(vm, instr);
    }
}
