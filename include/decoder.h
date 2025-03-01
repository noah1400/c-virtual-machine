#ifndef _DECODER_H_
#define _DECODER_H_

#include "vm_types.h"

// Decode a 32-bit instruction at the specified memory address
int vm_decode_instruction(VM *vm, uint16_t address, Instruction *instr);

// Fetch and decode the instruction at the program counter
uint32_t vm_fetch_instruction(VM *vm);

// Encode an instruction structure back to binary format
uint32_t vm_encode_instruction(Instruction *instr);

// Disassemble an instruction into human-readable text (for debugging)
void vm_disassemble_instruction(VM *vm, Instruction *instr, char *buffer, size_t buffer_size);

// Get mnemonic string for an opcode
const char* vm_opcode_to_mnemonic(uint8_t opcode);

#endif // _DECODER_H_