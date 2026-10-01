; PUSHM in compiled code with SP above the top of the stack faults, though the words it writes start below it
; vm-args: -j 1
; expect-exit: 1

.text
    MFCR R9, SHI
    ADD R9, #4
    LOAD R10, #3
.loop:
    PUSHM R10, R11
    POPM R10, R11
    DEC R10
    JNZ .loop
    MOVE SP, R9
    PUSHM R10, R11
    HALT
