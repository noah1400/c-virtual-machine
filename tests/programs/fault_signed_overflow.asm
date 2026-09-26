; expect-exit: 1
; expect-stderr: Signed division overflow
.text
    LOAD R8, #1
    SHL R8, #31
    LOAD R9, #1
    NEG R9
    IDIV R8, R9
