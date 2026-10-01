; A push in compiled code with SP one byte above the top of the stack faults
; vm-args: -j 1
; expect-exit: 1

.text
    MFCR R9, SHI
    ADD R9, #1
    LOAD R10, #3
.loop:
    PUSH R10
    POP R11
    DEC R10
    JNZ .loop
    MOVE SP, R9
    PUSH R10
    HALT
