; Conditional assembly driven by constants, including ones given on the command line
; asm-args: -DVERBOSE -D LEVEL=2

.equ MODE, 3

.macro SHOW value
.if \value > 9
    LOAD R0, big_text
.else
    LOAD R0, small_text
.endif
    SYSCALL #2
.endm

.text
.ifdef VERBOSE
    LOAD R8, #1
.else
    LOAD R8, #0
.endif
    CALL print_int

.if LEVEL >= 2 && MODE == 3
    LOAD R8, #LEVEL * 10 + MODE
.else
    LOAD R8, #-1
.endif
    CALL print_int

.ifndef QUIET
.if MODE != 3
    LOAD R8, #100
.else
    LOAD R8, #200
.endif
.else
    LOAD R8, #300
.endif
    CALL print_int

; Decided in pass 1, so a definition further down does not count
.ifdef LATER
    LOAD R8, #1
.else
    LOAD R8, #0
.endif
    CALL print_int

    SHOW 5
    SHOW 50
    LOAD R8, #!0 + (3 < 4) + (4 <= 4) + (5 > 6) + (7 >= 8) + (1 != 1) + (2 == 2)
    CALL print_int
    HALT

.equ LATER, 1

.include "lib.inc"

.data
small_text:
    .asciiz "small\n"
big_text:
    .asciiz "big\n"
