; expect-exit: 1
; expect-stderr: Unaligned program counter 0x0002
.text
    LOAD R8, #2
    JMP R8
