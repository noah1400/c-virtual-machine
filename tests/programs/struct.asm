; .struct names the offsets of fields as STRUCTURE.FIELD, and the structure's name is its size

.struct Entry
name:   .space 24
size:   .dword 0
flags:  .byte 0
.align 4
.ends

.text
    LOAD R8, #Entry.name
    CALL print_int
    LOAD R8, #Entry.size
    CALL print_int
    LOAD R8, #Entry.flags
    CALL print_int
    LOAD R8, #Entry
    CALL print_int

    ; The second of three entries
    LOAD R10, entries + Entry
    LOAD R9, #42
    STORE R9, [R10 + Entry.size]
    LOAD R8, [entries + Entry + Entry.size]
    CALL print_int
    HALT

.include "lib.inc"

.data
entries:
    .space Entry * 3
