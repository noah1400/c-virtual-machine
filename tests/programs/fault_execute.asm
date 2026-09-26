; expect-exit: 1
; expect-stderr: required permission 0x04
.text
    ALLOC R8, #16
    PROTECT R8, #3
    JMP R8
