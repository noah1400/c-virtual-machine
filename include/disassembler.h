#ifndef _DISASSEMBLER_H_
#define _DISASSEMBLER_H_

#include <stdint.h>

typedef struct {
    char **names;      // Symbol names
    uint32_t *addresses; // Symbol addresses
    uint8_t *types;      // Symbol types (0=code, 1=data)
    uint32_t count;      // Number of symbols
} SymbolTable;

// Print register name with optional suffix
void print_register(uint8_t reg, int with_suffix);

// Disassemble a single instruction
void disassemble_instruction(uint32_t address, uint32_t instruction);

// Disassemble a section of memory
void disassemble_memory(uint8_t *memory, uint32_t start_addr, uint32_t length, SymbolTable *symbols);;

void disassemble_data(uint8_t *memory, uint32_t start_addr, uint32_t length);

void disassemble_dump_memory(uint8_t *memory, uint32_t addr, uint32_t count);

// Load a binary file for disassembly
uint8_t* load_binary_file(const char *filename, uint32_t *size);

// Main function for disassembler
int disassemble_file(const char *filename);

void parse_symbol_table(const uint8_t *data, uint32_t size, SymbolTable *table);

void free_symbol_table(SymbolTable *table);

const char* disassemble_find_symbol_for_address(SymbolTable *table, uint32_t address);

#endif // _DISASSEMBLER_H_