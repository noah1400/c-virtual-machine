; expect-exit: 1
; expect-stderr: outside the stack segment
.text
    LOAD SP, #0x5000
    PUSH R0
