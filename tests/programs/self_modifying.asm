; Code that rewrites its own instructions while it runs: the VM has to run the new ones, also when only
; the extension word of an instruction changes and when a routine is copied over another one

.text
    ; Each round raises the immediate of the ADD that the next round runs: 1 + 2 + ... + 10
    LOAD R9, #0
    LOAD R10, #1
.add:
    ADD R9, #1
    INC R10
    LOAD R0, [.add]
    AND R0, #0xFFFF0000
    OR R0, R10
    STORE R0, [.add]
    CMP R10, #11
    JNZ .add
    MOVE R8, R9
    CALL print_int

    ; An immediate too wide for the instruction word lives in the extension word after it
    LOAD R9, #0
    LOAD R10, #0
.wide:
    ADD R9, #100000
    LOAD R0, [.wide+4]
    ADD R0, #100000
    STORE R0, [.wide+4]
    INC R10
    CMP R10, #3
    JNZ .wide
    MOVE R8, R9
    CALL print_int

    ; A routine copied over another one that already ran
    CALL .routine
    MOVE R8, R0
    CALL print_int
    LOAD R6, .routine
    LOAD R7, .other
    MEMCPY R6, R7, #8
    CALL .routine
    MOVE R8, R0
    CALL print_int
    HALT

.routine:
    LOAD R0, #1
    RET
.other:
    LOAD R0, #2
    RET

.include "lib.inc"
