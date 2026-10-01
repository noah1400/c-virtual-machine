; Loads in compiled code that move up to the start of the heap: one that reaches into it faults
; vm-args: -j 1
; expect-exit: 1

.text
    LOAD R0, #16
    SYSCALL #20
    MFCR R9, HEAPLO
    SUB R9, #5
    LOAD R10, #4
.loop:
    LOAD R8, [R9]
    CALL print_hex
    INC R9
    DEC R10
    JNZ .loop
    HALT

.include "lib.inc"
