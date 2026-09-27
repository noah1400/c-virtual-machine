; expect-exit: 1
; expect-stderr: outside the stack
.text
    LOAD SP, #0x5000
    PUSH R0
