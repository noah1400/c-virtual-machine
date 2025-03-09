#include <debug.h>

void load_debug_symbols(VM *vm, const uint8_t *data, uint32_t size) {
    if (!vm || !data || size < 4) {
        return;
    }
    
    // Allocate debug info structure
    vm->debug_info = (DebugInfo*)malloc(sizeof(DebugInfo));
    if (!vm->debug_info) {
        return;  // Memory allocation failure
    }
    
    memset(vm->debug_info, 0, sizeof(DebugInfo));
    
    const uint8_t *ptr = data;
    
    // Read symbol count
    uint32_t symbol_count = *((uint32_t*)ptr);
    ptr += 4;
    
    // Allocate symbol array
    vm->debug_info->symbols = (Symbol*)malloc(symbol_count * sizeof(Symbol));
    if (!vm->debug_info->symbols) {
        free(vm->debug_info);
        vm->debug_info = NULL;
        return;
    }
    
    vm->debug_info->symbol_count = symbol_count;
    
    // Read symbols
    for (uint32_t i = 0; i < symbol_count && ptr < data + size; i++) {
        // Name length
        uint16_t name_len = *((uint16_t*)ptr);
        ptr += 2;
        
        if (ptr + name_len >= data + size) {
            break;  // Bounds check
        }
        
        // Allocate and copy name
        vm->debug_info->symbols[i].name = (char*)malloc(name_len + 1);
        if (!vm->debug_info->symbols[i].name) {
            continue;  // Skip if allocation fails
        }
        
        memcpy(vm->debug_info->symbols[i].name, ptr, name_len);
        vm->debug_info->symbols[i].name[name_len] = '\0';
        ptr += name_len;
        
        // Address
        if (ptr + 4 >= data + size) break;
        vm->debug_info->symbols[i].address = *((uint32_t*)ptr);
        ptr += 4;
        
        // Type
        if (ptr >= data + size) break;
        vm->debug_info->symbols[i].type = *ptr++;
        
        // Line number
        if (ptr + 4 >= data + size) break;
        vm->debug_info->symbols[i].line_num = *((uint32_t*)ptr);
        ptr += 4;
        
        // Source file path length
        if (ptr + 2 >= data + size) break;
        uint16_t file_len = *((uint16_t*)ptr);
        ptr += 2;
        
        // Source file path
        if (file_len > 0) {
            if (ptr + file_len >= data + size) break;
            
            vm->debug_info->symbols[i].source_file = (char*)malloc(file_len + 1);
            if (vm->debug_info->symbols[i].source_file) {
                memcpy(vm->debug_info->symbols[i].source_file, ptr, file_len);
                vm->debug_info->symbols[i].source_file[file_len] = '\0';
            }
            ptr += file_len;
        } else {
            vm->debug_info->symbols[i].source_file = NULL;
        }
    }
    
    // Read source line count
    if (ptr + 4 >= data + size) {
        return;  // No source line info
    }
    
    uint32_t line_count = *((uint32_t*)ptr);
    ptr += 4;
    
    // Allocate source line array
    vm->debug_info->source_lines = (SourceLine*)malloc(line_count * sizeof(SourceLine));
    if (!vm->debug_info->source_lines) {
        return;  // Partial success - we still have symbols
    }
    
    vm->debug_info->source_line_count = line_count;
    
    // Read source lines
    for (uint32_t i = 0; i < line_count && ptr < data + size; i++) {
        // Address
        if (ptr + 4 >= data + size) break;
        vm->debug_info->source_lines[i].address = *((uint32_t*)ptr);
        ptr += 4;
        
        // Line number
        if (ptr + 4 >= data + size) break;
        vm->debug_info->source_lines[i].line_num = *((uint32_t*)ptr);
        ptr += 4;
        
        // Source text length
        if (ptr + 2 >= data + size) break;
        uint16_t source_len = *((uint16_t*)ptr);
        ptr += 2;
        
        if (ptr + source_len >= data + size) {
            break;  // Bounds check
        }
        
        // Allocate and copy source text
        vm->debug_info->source_lines[i].source = (char*)malloc(source_len + 1);
        if (!vm->debug_info->source_lines[i].source) {
            continue;  // Skip if allocation fails
        }
        
        memcpy(vm->debug_info->source_lines[i].source, ptr, source_len);
        vm->debug_info->source_lines[i].source[source_len] = '\0';
        ptr += source_len;
        
        // Source file path length
        if (ptr + 2 >= data + size) break;
        uint16_t file_len = *((uint16_t*)ptr);
        ptr += 2;
        
        // Source file path
        if (file_len > 0) {
            if (ptr + file_len >= data + size) break;
            
            vm->debug_info->source_lines[i].source_file = (char*)malloc(file_len + 1);
            if (vm->debug_info->source_lines[i].source_file) {
                memcpy(vm->debug_info->source_lines[i].source_file, ptr, file_len);
                vm->debug_info->source_lines[i].source_file[file_len] = '\0';
            }
            ptr += file_len;
        } else {
            vm->debug_info->source_lines[i].source_file = NULL;
        }
    }
}

// Function to free debug info
void free_debug_info(VM *vm) {
    if (!vm || !vm->debug_info) {
        return;
    }
    
    // Free symbols
    if (vm->debug_info->symbols) {
        for (uint32_t i = 0; i < vm->debug_info->symbol_count; i++) {
            free(vm->debug_info->symbols[i].name);
            free(vm->debug_info->symbols[i].source_file);
        }
        free(vm->debug_info->symbols);
    }
    
    // Free source lines
    if (vm->debug_info->source_lines) {
        for (uint32_t i = 0; i < vm->debug_info->source_line_count; i++) {
            free(vm->debug_info->source_lines[i].source);
            free(vm->debug_info->source_lines[i].source_file);
        }
        free(vm->debug_info->source_lines);
    }
    
    // Free debug info structure
    free(vm->debug_info);
    vm->debug_info = NULL;
}

// Helper function to find symbol by address
Symbol* find_symbol_by_address(VM *vm, uint32_t address) {
    if (!vm || !vm->debug_info) {
        return NULL;
    }
    
    // Find closest symbol before the address
    Symbol *closest = NULL;
    uint32_t closest_distance = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < vm->debug_info->symbol_count; i++) {
        Symbol *sym = &vm->debug_info->symbols[i];
        
        // Symbol must be before the address
        if (sym->address <= address) {
            uint32_t distance = address - sym->address;
            
            // Update if this is closer
            if (distance < closest_distance) {
                closest = sym;
                closest_distance = distance;
            }
        }
    }
    
    return closest;
}

// Function to find source line by address
SourceLine* find_source_line_by_address(VM *vm, uint32_t address) {
    if (!vm || !vm->debug_info) {
        return NULL;
    }
    
    // Find a direct match first
    for (uint32_t i = 0; i < vm->debug_info->source_line_count; i++) {
        SourceLine *line = &vm->debug_info->source_lines[i];
        if (line->address == address) {
            return line;
        }
    }
    
    // If no exact match, find closest line before the address
    SourceLine *closest = NULL;
    uint32_t closest_distance = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < vm->debug_info->source_line_count; i++) {
        SourceLine *line = &vm->debug_info->source_lines[i];
        
        // Line must be before the address
        if (line->address <= address) {
            uint32_t distance = address - line->address;
            
            // Update if this is closer
            if (distance < closest_distance) {
                closest = line;
                closest_distance = distance;
            }
        }
    }
    
    return closest;
}