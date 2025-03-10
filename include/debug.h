#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <vm_types.h>
#include <string.h>
#include <stdlib.h>

void load_debug_symbols(VM *vm, const uint8_t *data, uint32_t size);

void free_debug_info(VM *vm);

Symbol* find_symbol_by_address(VM *vm, uint32_t address);

SourceLine* find_source_line_by_address(VM *vm, uint32_t address);

void debug_print_source_info(VM *vm);

void debug_dump_source_mapping(VM *vm);

#endif // _DEBUG_H_