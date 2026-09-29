#include <stdio.h>
#include "alu.h"
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

// Instructions of a register and an operand, which CMP and TEST leave unchanged
static int execute_binary(VM *vm, const Instruction *instr) {
    uint32_t *dest = &vm->registers[instr->reg1];
    uint32_t b = read_operand(vm, instr, instr->reg2, 4);
    uint8_t opcode = instr->opcode;

    if (vm->last_error != VM_ERROR_NONE) {
        return vm->last_error;
    }
    if (opcode == DIV_OP || opcode == MOD_OP || opcode == IDIV_OP || opcode == IMOD_OP) {
        if (b == 0) {
            return vm_raise(vm, VM_ERROR_DIVISION_BY_ZERO,
                            opcode == DIV_OP || opcode == IDIV_OP ? "Division by zero" : "Modulo by zero");
        }
        if (!alu_divides(opcode, *dest, b)) {
            return vm_raise(vm, VM_ERROR_DIVISION_BY_ZERO, "Signed division overflow");
        }
    }

    uint32_t result = alu_binary(opcode, &vm->registers[R4_SR], *dest, b);
    if (opcode != CMP_OP && opcode != TEST_OP) {
        *dest = result;
    }
    return VM_ERROR_NONE;
}

static int execute_float(VM *vm, const Instruction *instr) {
    uint32_t operand = 0;

    if (instr->opcode != FNEG_OP && instr->opcode != FABS_OP) {
        operand = read_operand(vm, instr, instr->reg2, 4);
        if (vm->last_error != VM_ERROR_NONE) {
            return vm->last_error;
        }
    }
    alu_float(instr->opcode, &vm->registers[R4_SR], &vm->registers[instr->reg1], operand);
    return VM_ERROR_NONE;
}

static int execute_bit_count(VM *vm, const Instruction *instr) {
    uint32_t value = read_operand(vm, instr, instr->reg2, 4);
    if (vm->last_error != VM_ERROR_NONE) {
        return vm->last_error;
    }
    vm->registers[instr->reg1] = alu_count(instr->opcode, &vm->registers[R4_SR], value);
    return VM_ERROR_NONE;
}

static int execute_unary(VM *vm, const Instruction *instr) {
    uint32_t *dest = &vm->registers[instr->reg1];
    *dest = alu_unary(instr->opcode, &vm->registers[R4_SR], *dest);
    return VM_ERROR_NONE;
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
            if (vm->last_error == VM_ERROR_NONE) {
                cpu_pop_frames(vm, 0);
            }
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
                if (vm->last_error == VM_ERROR_NONE) {
                    cpu_push_frame(vm, vm->error_pc, vm->registers[R3_PC], -1);
                }
            }
            if (vm->last_error == VM_ERROR_NONE && alu_condition(vm->registers[R4_SR], instr->opcode)) {
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
            cpu_push_registers(vm, 0, 15);
            break;
        case POPA_OP:
            cpu_pop_registers(vm, 0, 15);
            break;
        case PUSHM_OP:
            cpu_push_registers(vm, instr->reg1, instr->reg2);
            break;
        case POPM_OP:
            cpu_pop_registers(vm, instr->reg1, instr->reg2);
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
            // Version 2.0; R5 features: I/O ports, heap protection, interrupts, syscalls, exceptions,
            // user mode, paging, floating point; R6 features: debug support, then the timer, display,
            // keyboard and disk devices
            vm->registers[R0_ACC] = 0x00020000;
            vm->registers[R5] = 0x00000004 | 0x00000008 | 0x00000010 | 0x00000020 | 0x00000040 | 0x00000080 |
                                0x00000100 | 0x00000200;
            vm->registers[R6] = 0x00000001 | 0x00000002 | 0x00000004 | 0x00000008 | 0x00000010;
            vm->registers[R7] = 0;
            break;
        case 2:
            // Memory size, page size and stack size
            vm->registers[R0_ACC] = vm->memory_size;
            vm->registers[R5] = VM_PAGE_SIZE;
            vm->registers[R6] = vm->stack_size;
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
            // Instruction count, state flags (2: debug mode, 4: interrupts enabled, 8: supervisor mode,
            // 16: paging), last error
            vm->registers[R0_ACC] = vm->instruction_count;
            vm->registers[R5] = (vm->debug_mode ? 0x02 : 0) | (cpu_get_flag(vm, INT_FLAG) ? 0x04 : 0) |
                                (cpu_get_flag(vm, SYS_FLAG) ? 0x08 : 0) | (vm->control[CR_PTB] ? 0x10 : 0);
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
            cpu_return_from_interrupt(vm);
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
    if (info->privileged && !cpu_get_flag(vm, SYS_FLAG)) {
        return vm_raise(vm, VM_ERROR_PRIVILEGE, "%s is not allowed in user mode", info->mnemonic);
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
        case BSWAP_OP:
            return execute_unary(vm, instr);

        case POPCNT_OP:
        case CLZ_OP:
        case CTZ_OP:
            return execute_bit_count(vm, instr);

        case SET_OP:
            if (!isa_is_conditional_jump((uint8_t)instr->immediate) || instr->immediate > 0xFF) {
                return vm_raise(vm, VM_ERROR_INVALID_INSTRUCTION, "Invalid SET condition 0x%X", instr->immediate);
            }
            vm->registers[instr->reg1] = (uint32_t)alu_condition(vm->registers[R4_SR], (uint8_t)instr->immediate);
            return VM_ERROR_NONE;

        default:
            if (instr->opcode >= FADD_OP && instr->opcode <= FTOI_OP) {
                return execute_float(vm, instr);
            }
            break;
    }

    // The top three opcode bits select the instruction group
    switch (instr->opcode >> 5) {
        case 1:
        case 2:
            return execute_binary(vm, instr);
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
