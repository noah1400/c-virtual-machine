; Macros with parameters, unique local labels and nested invocations

.macro PRINT_STRING address
    LOAD R0, \address
    SYSCALL #2
.endm

.macro NEWLINE
    LOAD R0, #'\n'
    SYSCALL #0
.endm

; Prints the number in a register, or "zero" when it is 0
.macro PRINT_NUMBER reg
    CMP \reg, #0
    JNZ .number\@
    PRINT_STRING zero_text
    JMP .done\@
.number\@:
    MOVE R0, \reg
    SYSCALL #1
.done\@:
    NEWLINE
.endm

.macro SWAP a, b
    MOVE R14, \a
    MOVE \a, \b
    MOVE \b, R14
.endm

.text
start:
    PRINT_STRING greeting
    NEWLINE
    LOAD R8, #7
    LOAD R9, #0
    SWAP R8, R9
    PRINT_NUMBER R8
    PRINT_NUMBER R9
after: PRINT_NUMBER R8
    LOAD R8, #after
    CALL print_int
    HALT

.include "lib.inc"

.data
greeting:
    .asciiz "macros, with commas; and semicolons"
zero_text:
    .asciiz "zero"
