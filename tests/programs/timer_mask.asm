; Timer requests wait while interrupts are disabled and arrive once after STI

.equ VECTOR, 0x21

.entry main

.text
.org 0x0100 + VECTOR * 4
    .dword handler

main:
    LOAD R8, #VECTOR
    OUT #0x41, R8
    LOAD R8, #5
    OUT #0x40, R8
    LOAD R10, #50
spin:
    LOOP R10, spin
    LOAD R8, #0
    OUT #0x40, R8
    LOAD R8, [count]
    CALL print_int
    STI
    NOP
    CLI
    LOAD R8, [count]
    CALL print_int
    IN R8, #0x42
    CALL print_int
    HALT

handler:
    LOAD R9, [count]
    INC R9
    STORE R9, [count]
    IRET

.include "lib.inc"

.data
count:
    .dword 0
