; The clock syscall follows host time across a sleep

.text
    SYSCALL #32
    MOVE R9, R0
    LOAD R0, #30
    SYSCALL #31
    SYSCALL #32
    SUB R0, R9
    LOAD R8, #1
    CMP R0, #30
    JAE done
    LOAD R8, #0
done:
    CALL print_int
    HALT

.include "lib.inc"
