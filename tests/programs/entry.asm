; Execution starts at the .entry label and RESET returns there with memory intact

.entry start

.text
greet:
    LOAD R0, #'X'
    SYSCALL #0
    RET

start:
    CALL greet
    LOAD R8, [resets]
    INC R8
    STORE R8, [resets]
    CMP R8, #3
    JAE done
    RESET
done:
    LOAD R0, #'\n'
    SYSCALL #0
    CALL print_int
    HALT

.include "lib.inc"

.data
resets:
    .dword 0
