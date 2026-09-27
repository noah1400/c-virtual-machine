; .rept repeats the lines up to .endr a number of times that constants defined before it can give

.equ ROWS, 2
.equ COLUMNS, ROWS + 1

.text
    LOAD R8, #0
.rept ROWS
.rept COLUMNS
    INC R8
.endr
    ADD R8, #10
.endr
    CALL print_int
    LOADB R8, [table + 3]
    CALL print_int
    LOAD R8, #table_end - table
    CALL print_int
    HALT

.include "lib.inc"

.data
table:
.rept 4
    .byte 7
.endr
.rept 0
    .byte 9
.endr
table_end:
