#ifndef _DISASSEMBLER_H_
#define _DISASSEMBLER_H_

#include <stdint.h>

// Print register name with optional suffix
void print_register(uint8_t reg, int with_suffix);

// Disassemble a single instruction
void disassemble_instruction(uint32_t address, uint32_t instruction);

// Disassemble a section of memory
void disassemble_memory(uint8_t *memory, uint32_t start_addr, uint32_t length);

// Load a binary file for disassembly
uint8_t* load_binary_file(const char *filename, uint32_t *size);

// Main function for disassembler
int disassemble_file(const char *filename);

#endif // _DISASSEMBLER_H_