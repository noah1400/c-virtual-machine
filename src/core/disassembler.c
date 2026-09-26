#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "binfmt.h"
#include "disassembler.h"
#include "vm_types.h"

static int is_branch(uint8_t opcode) {
    return (opcode >= JMP_OP && opcode <= CALL_OP) || opcode == LOOP_OP;
}

static void format_displacement(char *out, size_t size, const char *base, int32_t displacement) {
    if (displacement == 0) {
        snprintf(out, size, "[%s]", base);
    } else {
        snprintf(out, size, "[%s%+d]", base, displacement);
    }
}

static void format_operand(char *out, size_t size, const Instruction *instr, uint8_t reg,
                           const DebugInfo *info, int target) {
    const Symbol *sym;

    switch (instr->mode) {
        case IMM_MODE:
            if (target && (sym = debug_symbol_at(info, instr->immediate))) {
                snprintf(out, size, "%s", sym->name);
            } else if (target) {
                snprintf(out, size, "0x%04X", instr->immediate);
            } else {
                snprintf(out, size, "#0x%X", instr->immediate);
            }
            break;
        case REG_MODE:
            snprintf(out, size, "%s", isa_register_name(reg));
            break;
        case MEM_MODE:
            if ((sym = debug_symbol_at(info, instr->immediate))) {
                snprintf(out, size, "[%s]", sym->name);
            } else {
                snprintf(out, size, "[0x%04X]", instr->immediate);
            }
            break;
        case REGM_MODE:
            snprintf(out, size, "[%s]", isa_register_name(reg));
            break;
        case IDX_MODE:
        case STK_MODE:
        case BAS_MODE:
            format_displacement(out, size,
                                instr->mode == IDX_MODE ? isa_register_name(reg)
                                                        : isa_register_name(instr->mode == STK_MODE ? R2_SP : R1_BP),
                                isa_displacement(instr));
            break;
        default:
            snprintf(out, size, "<mode %u>", instr->mode);
            break;
    }
}

void disasm_format(const Instruction *instr, const DebugInfo *info, char *buffer, size_t size) {
    const InstructionInfo *op = isa_by_opcode(instr->opcode);
    if (!op) {
        snprintf(buffer, size, ".dword 0x%08X", isa_encode(instr));
        return;
    }

    char operand[96] = "";
    const char *r1 = isa_register_name(instr->reg1);
    const char *r2 = isa_register_name(instr->reg2);
    int annotate = 0;

    switch (op->format) {
        case FMT_NONE:
            snprintf(buffer, size, "%s", op->mnemonic);
            return;
        case FMT_REG:
            snprintf(buffer, size, "%s %s", op->mnemonic, r1);
            return;
        case FMT_IMM:
            snprintf(buffer, size, "%s #%u", op->mnemonic, instr->immediate);
            return;
        case FMT_OPT_IMM:
            if (instr->immediate) {
                snprintf(buffer, size, "%s #%u", op->mnemonic, instr->immediate);
            } else {
                snprintf(buffer, size, "%s", op->mnemonic);
            }
            return;
        case FMT_REG_REG:
            snprintf(buffer, size, "%s %s, %s", op->mnemonic, r1, r2);
            return;
        case FMT_REG_REG_SIZE:
            if (instr->mode == REG_MODE) {
                snprintf(buffer, size, "%s %s, %s, %s", op->mnemonic, r1, r2,
                         isa_register_name(instr->immediate & 0x0F));
            } else {
                snprintf(buffer, size, "%s %s, %s, #%u", op->mnemonic, r1, r2, instr->immediate & 0x0FFF);
            }
            return;
        case FMT_OPERAND:
            format_operand(operand, sizeof(operand), instr, instr->reg1, info, is_branch(instr->opcode));
            snprintf(buffer, size, "%s %s", op->mnemonic, operand);
            annotate = !is_branch(instr->opcode);
            break;
        case FMT_REG_OPERAND:
            format_operand(operand, sizeof(operand), instr, instr->reg2, info, is_branch(instr->opcode));
            snprintf(buffer, size, "%s %s, %s", op->mnemonic, r1, operand);
            annotate = !is_branch(instr->opcode);
            break;
        case FMT_OPERAND_REG:
            format_operand(operand, sizeof(operand), instr, instr->reg2, info, 0);
            snprintf(buffer, size, "%s %s, %s", op->mnemonic, operand, r1);
            return;
        default:
            snprintf(buffer, size, "%s ???", op->mnemonic);
            return;
    }

    // Immediates that equal a label address are most likely pointers to it
    const Symbol *sym;
    if (annotate && instr->mode == IMM_MODE && (sym = debug_symbol_at(info, instr->immediate))) {
        size_t len = strlen(buffer);
        snprintf(buffer + len, size - len, "  ; %s", sym->name);
    }
}

void disasm_hexdump(const uint8_t *bytes, uint32_t address, uint32_t count) {
    for (uint32_t row = 0; row < count; row += 16) {
        uint32_t n = count - row < 16 ? count - row : 16;

        printf("0x%04X: ", address + row);
        for (uint32_t i = 0; i < 16; i++) {
            if (i < n) {
                printf("%02X ", bytes[row + i]);
            } else {
                printf("   ");
            }
        }
        printf(" | ");
        for (uint32_t i = 0; i < n; i++) {
            uint8_t c = bytes[row + i];
            putchar(c >= 32 && c <= 126 ? c : '.');
        }
        printf("\n");
    }
}

// Next label strictly after the address within [address, end), or end
static uint32_t next_label(const DebugInfo *info, uint32_t address, uint32_t end) {
    uint32_t next = end;
    for (uint32_t i = 0; info && i < info->symbol_count; i++) {
        uint32_t a = info->symbols[i].address;
        if (info->symbols[i].type != SYMBOL_CONST && a > address && a < next) {
            next = a;
        }
    }
    return next;
}

static void print_label(const DebugInfo *info, uint32_t address) {
    const Symbol *sym = debug_symbol_at(info, address);
    if (sym) {
        printf("\n%s:\n", sym->name);
    }
}

int disassemble_file(const char *filename) {
    uint32_t size;
    const char *problem;
    uint8_t *buffer = read_binary_file(filename, &size, &problem);
    if (!buffer) {
        fprintf(stderr, "Error: %s: %s\n", problem, filename);
        return 1;
    }

    Vm32Image bin;
    problem = vm32_parse(buffer, size, &bin);
    if (problem) {
        fprintf(stderr, "Error: %s: %s\n", problem, filename);
        free(buffer);
        return 1;
    }

    DebugInfo *info = bin.symbol_size ? debug_info_parse(bin.symbols, bin.symbol_size) : NULL;

    printf("VM32 binary v%u.%u\n", bin.version_major, bin.version_minor);
    printf("  Code segment: 0x%04X, %u bytes\n", bin.code_base, bin.code_size);
    printf("  Data segment: 0x%04X, %u bytes\n", bin.data_base, bin.data_size);
    printf("  Symbol table: %u bytes", bin.symbol_size);
    if (info) {
        printf(" (%u symbols, %u source lines)", info->symbol_count, info->source_line_count);
    }
    printf("\n");

    if (bin.code_size > 0) {
        printf("\nCode:\n");
        for (uint32_t offset = 0; offset + 4 <= bin.code_size; offset += 4) {
            uint32_t address = bin.code_base + offset;
            uint32_t word = read_le32(bin.code + offset);
            Instruction instr;
            char text[160];

            print_label(info, address);
            isa_decode(word, &instr);
            disasm_format(&instr, info, text, sizeof(text));

            char *operands = strchr(text, ' ');
            if (operands) {
                *operands++ = '\0';
            }
            if (operands) {
                printf("  %04X  %08X  %-8s%s\n", address, word, text, operands);
            } else {
                printf("  %04X  %08X  %s\n", address, word, text);
            }
        }
    }

    if (bin.data_size > 0) {
        printf("\nData:\n");
        uint32_t end = bin.data_base + bin.data_size;
        for (uint32_t address = bin.data_base; address < end;) {
            uint32_t stop = next_label(info, address, end);
            print_label(info, address);
            disasm_hexdump(bin.data + (address - bin.data_base), address, stop - address);
            address = stop;
        }
    }

    debug_info_free(info);
    free(buffer);
    return 0;
}
