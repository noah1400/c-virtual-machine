; expect-exit: 1
; expect-stderr: Memory access violation: address 0x10000
.text
    LOAD R8, #1
    SHL R8, #16
    LOAD R9, [R8]
