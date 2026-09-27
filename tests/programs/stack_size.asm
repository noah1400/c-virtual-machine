; vm -S sets the room for the stack, which CPUID reports and SLO marks
; vm-args: -S 8

.text
    LOAD R0, #2
    CPUID
    MOVE R8, R6
    CALL print_int
    MFCR R8, SLO
    CALL print_int
    HALT

.include "lib.inc"
