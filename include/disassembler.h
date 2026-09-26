#ifndef _DISASSEMBLER_H_
#define _DISASSEMBLER_H_

#include <stddef.h>
#include <stdint.h>
#include "debug.h"
#include "instruction_set.h"

// Formats an instruction in assembler syntax, naming addresses with symbols when available
void disasm_format(const Instruction *instr, const DebugInfo *info, char *buffer, size_t size);

// Prints bytes as hex and ASCII, 16 per row, labelled with their VM addresses
void disasm_hexdump(const uint8_t *bytes, uint32_t address, uint32_t count);

// Prints the header, code listing and data dump of a VM32 binary
int disassemble_file(const char *filename);

#endif // _DISASSEMBLER_H_
