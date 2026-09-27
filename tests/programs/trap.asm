; With the trap flag set, vector 15 runs after every instruction until the flag is cleared

.equ TRAP_FLAG, 0x80

.text
    LOAD R8, vectors
    MTCR IVTB, R8

    PUSHF
    POP R8
    OR R8, #TRAP_FLAG
    PUSH R8
    POPF
    NOP
    NOP
    NOP
    LOAD R9, #0
    PUSH R9
    POPF
    LOAD R8, [steps]
    CALL print_int
    HALT

on_trap:
    LOAD R10, [steps]
    INC R10
    STORE R10, [steps]
    IRET

.include "lib.inc"

.data
vectors:
    .space 15 * 4
    .dword on_trap
    .space 240 * 4
steps:
    .dword 0
