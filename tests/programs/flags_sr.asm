; The flags that instructions leave behind reach SR for whatever reads them next: PUSHF, SR as a register,
; a jump after a syscall, and instructions that keep some flags or add the carry

.text
    ; ADD overflows into the sign
    LOAD R5, #0x7FFFFFFF
    ADD R5, #1
    CALL print_flags

    ; SR read as a register
    LOAD R5, #7
    SUB R5, #7
    MOVE R8, R4
    AND R8, #0x0F
    CALL print_int

    ; A compare holds across a syscall
    LOAD R5, #2
    CMP R5, #3
    LOAD R0, #'<'
    SYSCALL #0
    JL .less
    LOAD R0, #'?'
    SYSCALL #0
.less:
    LOAD R0, #'\n'
    SYSCALL #0

    ; MUL and a shift by 0 keep the carry of the ADD before them
    LOAD R5, #0xFFFFFFFF
    ADD R5, #1
    LOAD R6, #3
    MUL R6, #5
    CALL print_flags
    LOAD R5, #0xFFFFFFFF
    ADD R5, #1
    LOAD R6, #0x80000000
    SHL R6, #0
    CALL print_flags

    ; A shift after a shift sets the carry again
    LOAD R6, #0x80000000
    SHL R6, #1
    LOAD R7, #1
    SHL R7, #1
    CALL print_flags

    ; ADDC adds the carry of the ADD before it, and SET reads a compare
    LOAD R5, #0xFFFFFFFF
    ADD R5, #1
    LOAD R8, #40
    ADDC R8, #1
    CALL print_int
    LOAD R5, #-5
    CMP R5, #2
    SETL R8
    CALL print_int
    HALT

.include "lib.inc"
