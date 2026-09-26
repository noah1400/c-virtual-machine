#include <stdio.h>
#include "debug.h"

typedef struct {
    const uint8_t *ptr;
    const uint8_t *end;
    bool ok;
} Reader;

static uint32_t read_uint(Reader *r, int bytes) {
    if (!r->ok || r->end - r->ptr < bytes) {
        r->ok = false;
        return 0;
    }
    uint32_t value = 0;
    for (int i = 0; i < bytes; i++) {
        value |= (uint32_t)r->ptr[i] << (8 * i);
    }
    r->ptr += bytes;
    return value;
}

static char *read_string(Reader *r) {
    uint16_t len = (uint16_t)read_uint(r, 2);
    if (!r->ok || r->end - r->ptr < len) {
        r->ok = false;
        return NULL;
    }
    char *str = malloc(len + 1u);
    if (!str) {
        r->ok = false;
        return NULL;
    }
    memcpy(str, r->ptr, len);
    str[len] = '\0';
    r->ptr += len;
    return str;
}

void load_debug_symbols(VM *vm, const uint8_t *data, uint32_t size) {
    if (!vm || !data) {
        return;
    }

    DebugInfo *info = calloc(1, sizeof(DebugInfo));
    if (!info) {
        return;
    }
    vm->debug_info = info;

    Reader r = { data, data + size, true };

    // Entries have a minimum encoded size, which bounds allocations for corrupt counts
    uint32_t symbol_count = read_uint(&r, 4);
    if (!r.ok || symbol_count > size / 13) {
        return;
    }
    info->symbols = calloc(symbol_count ? symbol_count : 1, sizeof(Symbol));
    if (!info->symbols) {
        return;
    }
    for (uint32_t i = 0; i < symbol_count; i++) {
        Symbol *sym = &info->symbols[i];
        sym->name = read_string(&r);
        sym->address = read_uint(&r, 4);
        sym->type = (uint8_t)read_uint(&r, 1);
        sym->line_num = read_uint(&r, 4);
        sym->source_file = read_string(&r);
        if (!r.ok) {
            free(sym->name);
            free(sym->source_file);
            return;
        }
        if (sym->source_file && sym->source_file[0] == '\0') {
            free(sym->source_file);
            sym->source_file = NULL;
        }
        info->symbol_count = i + 1;
    }

    uint32_t line_count = read_uint(&r, 4);
    if (!r.ok || line_count > size / 12) {
        return;
    }
    info->source_lines = calloc(line_count ? line_count : 1, sizeof(SourceLine));
    if (!info->source_lines) {
        return;
    }
    for (uint32_t i = 0; i < line_count; i++) {
        SourceLine *line = &info->source_lines[i];
        line->address = read_uint(&r, 4);
        line->line_num = read_uint(&r, 4);
        line->source = read_string(&r);
        line->source_file = read_string(&r);
        if (!r.ok) {
            free(line->source);
            free(line->source_file);
            return;
        }
        if (line->source_file && line->source_file[0] == '\0') {
            free(line->source_file);
            line->source_file = NULL;
        }
        info->source_line_count = i + 1;
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
    
    if (vm->debug_mode > 1) {  // Extra verbose debugging
        printf("\nLooking for source line at address: 0x%04X\n", address);
    }
    
    // First, look for exact address match
    SourceLine *best_match = NULL;
    int best_score = -1;
    
    for (uint32_t i = 0; i < vm->debug_info->source_line_count; i++) {
        SourceLine *line = &vm->debug_info->source_lines[i];
        
        if (line->address == address) {
            // Skip invalid entries
            if (!line->source) {
                continue;
            }
            
            // Skip .include directives
            if (strstr(line->source, ".include") != NULL) {
                continue;
            }
            
            int score = 10;  // Base score for exact address match
            
            // Check if this is from an included file (not main.asm)
            if (line->source_file) {
                score += 5;  // Having a source file is good
                
                // Prefer included files over main.asm
                char *filename = strrchr(line->source_file, '/');
                if (!filename) {
                    filename = strrchr(line->source_file, '\\');
                }
                filename = filename ? filename + 1 : line->source_file;
                
                // Included files are more specific and get higher priority
                if (strcmp(filename, "main.asm") != 0) {
                    score += 50;  // Much higher priority for included files
                    
                    if (vm->debug_mode > 1) {
                        printf("Found included file match: %s (score %d)\n", filename, score);
                    }
                }
            }
            
            // Keep the highest scoring match
            if (score > best_score) {
                best_match = line;
                best_score = score;
            }
        }
    }
    
    // If we found an exact address match, return it
    if (best_match) {
        if (vm->debug_mode > 1) {
            printf("Best exact match: 0x%04X line %d in %s\n", 
                best_match->address, best_match->line_num,
                best_match->source_file ? best_match->source_file : "(none)");
        }
        return best_match;
    }
    
    // If no exact match, find nearest line before this address
    // Group by source file to find most relevant matches
    typedef struct {
        SourceLine *line;
        uint32_t distance;
        char *filename;  // Just the basename for comparison
    } CandidateLine;
    
    CandidateLine candidates[50] = {0};  // Up to 50 different source files
    int candidate_count = 0;
    
    // Find the closest line before address for each source file
    for (uint32_t i = 0; i < vm->debug_info->source_line_count; i++) {
        SourceLine *line = &vm->debug_info->source_lines[i];
        
        // Skip invalid entries
        if (!line->source) {
            continue;
        }
        
        // Skip .include directives
        if (strstr(line->source, ".include") != NULL) {
            continue;
        }
        
        // Line must be before the address
        if (line->address <= address) {
            uint32_t distance = address - line->address;
            
            // Extract filename (basename)
            char *filename = NULL;
            if (line->source_file) {
                filename = strrchr(line->source_file, '/');
                if (!filename) {
                    filename = strrchr(line->source_file, '\\');
                }
                filename = filename ? filename + 1 : line->source_file;
            } else {
                // If no source file, use a placeholder
                filename = "unknown";
            }
            
            // Check if we already have a candidate for this file
            int file_idx = -1;
            for (int j = 0; j < candidate_count; j++) {
                if (candidates[j].filename && 
                    strcmp(candidates[j].filename, filename) == 0) {
                    file_idx = j;
                    break;
                }
            }
            
            if (file_idx >= 0) {
                // Update if this line is closer
                if (distance < candidates[file_idx].distance) {
                    candidates[file_idx].line = line;
                    candidates[file_idx].distance = distance;
                }
            } else if (candidate_count < 50) {
                // Add new source file candidate
                candidates[candidate_count].line = line;
                candidates[candidate_count].distance = distance;
                candidates[candidate_count].filename = filename;
                candidate_count++;
            }
        }
    }
    
    // Find closest overall match, with preference to included files
    SourceLine *closest = NULL;
    uint32_t closest_distance = 0xFFFFFFFF;
    int closest_score = -1;
    
    for (int i = 0; i < candidate_count; i++) {
        CandidateLine *candidate = &candidates[i];
        
        // Calculate score based on distance and filename
        int score = 0;
        
        // Closer is better - use inverse of distance as part of score
        // But max out at 10 to avoid overflow with very small distances
        score += 10 - (candidate->distance > 1000 ? 10 : candidate->distance / 100);
        
        // Prefer included files over main
        if (candidate->filename && strcmp(candidate->filename, "main.asm") != 0) {
            score += 50;  // Much higher priority for included files
        }
        
        if (score > closest_score || 
            (score == closest_score && candidate->distance < closest_distance)) {
            closest = candidate->line;
            closest_distance = candidate->distance;
            closest_score = score;
        }
    }
    
    if (vm->debug_mode > 1 && closest) {
        printf("Best closest match: 0x%04X (distance %u) line %d in %s\n", 
            closest->address, closest_distance, closest->line_num,
            closest->source_file ? closest->source_file : "(none)");
    }
    
    return closest;
}