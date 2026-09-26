; expect-exit: 1
; expect-stderr: Memory protection violation
.text
    ALLOC R8, #16
    PROTECT R8, #1
    STORE R8, [R8]
    HALT
