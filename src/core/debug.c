#include <debug.h>
#include <stdio.h>

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
    
    printf("Loading %u symbols...\n", symbol_count);
    
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
    
    printf("Loading %u source lines...\n", line_count);
    
    // Allocate source line array
    vm->debug_info->source_lines = (SourceLine*)malloc(line_count * sizeof(SourceLine));
    if (!vm->debug_info->source_lines) {
        return;  // Partial success - we still have symbols
    }
    
    vm->debug_info->source_line_count = line_count;
    
    // Create a map of unique file paths to help correct cross-references
    char *file_paths[100] = {NULL};  // Up to 100 unique source files
    int file_count = 0;
    
    // First pass: collect unique source file paths and properly initialize all entries
    for (uint32_t i = 0; i < line_count && ptr < data + size; i++) {
        SourceLine *line = &vm->debug_info->source_lines[i];
        
        // Initialize to NULL to prevent issues if we have to exit early
        line->source = NULL;
        line->source_file = NULL;
        
        // Address
        if (ptr + 4 >= data + size) break;
        line->address = *((uint32_t*)ptr);
        ptr += 4;
        
        // Line number
        if (ptr + 4 >= data + size) break;
        line->line_num = *((uint32_t*)ptr);
        ptr += 4;
        
        // Source text length
        if (ptr + 2 >= data + size) break;
        uint16_t source_len = *((uint16_t*)ptr);
        ptr += 2;
        
        if (ptr + source_len >= data + size) {
            break;  // Bounds check
        }
        
        // Allocate and copy source text
        line->source = (char*)malloc(source_len + 1);
        if (!line->source) {
            continue;  // Skip if allocation fails
        }
        
        memcpy(line->source, ptr, source_len);
        line->source[source_len] = '\0';
        ptr += source_len;
        
        // Source file path length
        if (ptr + 2 >= data + size) break;
        uint16_t file_len = *((uint16_t*)ptr);
        ptr += 2;
        
        // Source file path
        if (file_len > 0) {
            if (ptr + file_len >= data + size) break;
            
            // Check if we've already seen this path
            char temp_path[512] = {0};
            memcpy(temp_path, ptr, file_len < 511 ? file_len : 511);
            
            // Look for basename within the path
            char *basename = strrchr(temp_path, '/');
            if (!basename) {
                basename = strrchr(temp_path, '\\');
            }
            if (basename) {
                basename++;  // Skip the slash
            } else {
                basename = temp_path;  // Use the whole thing if no separator
            }
            
            // See if we already have this file
            int file_index = -1;
            for (int j = 0; j < file_count; j++) {
                char *existing_basename = strrchr(file_paths[j], '/');
                if (!existing_basename) {
                    existing_basename = strrchr(file_paths[j], '\\');
                }
                if (existing_basename) {
                    existing_basename++;
                } else {
                    existing_basename = file_paths[j];
                }
                
                // If we find a match by basename, use that
                if (strcmp(basename, existing_basename) == 0) {
                    file_index = j;
                    break;
                }
            }
            
            // If we don't have this file already, add it
            if (file_index == -1 && file_count < 100) {
                file_paths[file_count] = strdup(temp_path);
                file_index = file_count++;
                printf("New source file %d: %s (basename: %s)\n", 
                       file_index, file_paths[file_index], basename);
            }
            
            // Now use the consistent path
            if (file_index >= 0) {
                line->source_file = strdup(file_paths[file_index]);
            } else {
                // Fallback: just use what we have
                line->source_file = (char*)malloc(file_len + 1);
                if (line->source_file) {
                    memcpy(line->source_file, ptr, file_len);
                    line->source_file[file_len] = '\0';
                }
            }
            
            ptr += file_len;
        } else {
            line->source_file = NULL;
        }
        
        // Display debug info for every 100th line
        if (i % 100 == 0) {
            printf("Source line %d: addr=0x%04X file=%s line=%d src=%s\n", 
                   i, line->address, 
                   line->source_file ? line->source_file : "(none)",
                   line->line_num,
                   line->source ? (line->source[0] == '.' ? "<directive>" : line->source) : "(none)");
        }
    }
    
    // Clean up the file path array
    for (int i = 0; i < file_count; i++) {
        free(file_paths[i]);
    }
    
    printf("Debug symbols loaded: %u symbols, %u source lines\n", 
           vm->debug_info->symbol_count, vm->debug_info->source_line_count);
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

void debug_print_source_info(VM *vm) {
    if (!vm || !vm->debug_info) {
        printf("No debug information available\n");
        return;
    }
    
    printf("\n--- Debug Source Files Info ---\n");
    int unique_files = 0;
    char *previous_file = NULL;
    
    for (uint32_t i = 0; i < vm->debug_info->source_line_count; i++) {
        SourceLine *line = &vm->debug_info->source_lines[i];
        
        // Only print when we encounter a new filename to avoid flooding
        if (line->source_file && (!previous_file || 
            strcmp(line->source_file, previous_file) != 0)) {
            
            printf("Source file at addr 0x%04X: '%s'\n", 
                   line->address, line->source_file);
            
            previous_file = line->source_file;
            unique_files++;
            
            // Print the first line from this file
            printf("  Sample line: %s\n", line->source ? line->source : "(empty)");
        }
    }
    
    printf("Found %d unique source files among %d source lines\n", 
           unique_files, vm->debug_info->source_line_count);
}

void debug_dump_source_mapping(VM *vm) {
    if (!vm || !vm->debug_info) {
        printf("No debug information available\n");
        return;
    }
    
    printf("\n=== SOURCE MAPPING DUMP ===\n");
    
    // Count and display unique source files
    printf("Source files in debug info:\n");
    int unique_files = 0;
    char **file_list = malloc(sizeof(char*) * vm->debug_info->source_line_count);
    if (!file_list) {
        printf("Memory allocation error\n");
        return;
    }
    
    for (uint32_t i = 0; i < vm->debug_info->source_line_count; i++) {
        SourceLine *line = &vm->debug_info->source_lines[i];
        
        if (!line->source_file) continue;
        
        // Check if we've seen this file before
        int found = 0;
        for (int j = 0; j < unique_files; j++) {
            if (strcmp(file_list[j], line->source_file) == 0) {
                found = 1;
                break;
            }
        }
        
        if (!found) {
            file_list[unique_files++] = line->source_file;
            printf("  %d: %s\n", unique_files, line->source_file);
        }
    }
    
    printf("Total unique source files: %d\n\n", unique_files);
    free(file_list);
    
    // Show some sample address to source mappings
    printf("Sample address mappings:\n");
    for (uint32_t i = 0; i < vm->debug_info->source_line_count && i < 20; i++) {
        SourceLine *line = &vm->debug_info->source_lines[i];
        printf("  0x%04X -> Line %4d in %-20s: %s\n", 
               line->address, 
               line->line_num,
               line->source_file ? line->source_file : "(none)",
               line->source ? line->source : "(none)");
    }
    
    printf("... %d more mappings ...\n", vm->debug_info->source_line_count - 20);
    printf("=== END SOURCE MAPPING ===\n\n");
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