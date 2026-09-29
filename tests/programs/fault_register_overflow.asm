; expect-exit: 1
; expect-stderr: Stack overflow
.text
recurse:
    PUSHM R8, R15
    JMP recurse
