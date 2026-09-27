; The heap spans HEAPLO to HEAPHI; rewriting either forgets every block

.text
    ALLOC R11, #16
    MFCR R8, HEAPLO
    SUB R8, R11
    NEG R8
    CALL print_int              ; a guard gap precedes the first block

    MFCR R8, HEAPHI
    MTCR HEAPHI, R8
    ALLOC R12, #16
    MOVE R8, R12
    SUB R8, R11
    CALL print_int              ; the same address is handed out again

    ; An empty heap leaves the memory to the program
    MFCR R8, HEAPLO
    MTCR HEAPHI, R8
    LOAD R9, #77
    STORE R9, [R11 + 64]
    LOAD R8, [R11 + 64]
    CALL print_int
    LOAD R0, #16
    SYSCALL #20
    MOVE R8, R5
    CALL print_int
    HALT

.include "lib.inc"
