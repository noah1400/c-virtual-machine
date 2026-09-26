; Assembler features: expressions, constants, local labels, strings and layout directives

.equ SIZE, end_of_table - table     ; defined before the labels it uses
.equ TWICE, SIZE * 2

.text
    LOAD R8, #SIZE
    CALL print_int
    LOAD R8, #TWICE + (1 << 4) - 3 % 2
    CALL print_int
    LOAD R8, #~0xFF & 0xFFF | 0x1
    CALL print_hex
    LOAD R8, #'A' + 2
    CALL print_int
    LOAD R8, #-8 / 3 + 10
    CALL print_int

first:
    JMP .skip
    HALT
.skip:
    LOAD R8, #here - first
    CALL print_int
here:
    LOAD R8, #$ - here
    CALL print_int

second:
    JMP .skip
.skip:
    LOAD R0, strings
    SYSCALL #2
    LOAD R0, included_text
    SYSCALL #2
    LOAD R0, #'\n'
    SYSCALL #0

    LOAD R8, #aligned & 3
    CALL print_int
    LOADB R8, [filled+1]
    CALL print_hex
    LOAD R8, #after_org
    CALL print_hex
    LOAD R8, [table+4]
    CALL print_int
    HALT

.include "lib.inc"
.include "constants.inc"

    NOP

.data
table:
    .dword 1, 2, 3
end_of_table:
strings:
    .ascii "semi;colon, "
    .byte "tab\t", 0x2D, ' '
    .asciiz "\x41\x42\x43 "
    .byte 1
    .align 4
aligned:
filled:
    .space 3, 0x7E
    .org 0x4100
after_org:
    .word 0
