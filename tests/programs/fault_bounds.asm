; expect-exit: 1
; expect-stderr: Memory access violation: address 0x200000
.text
    LOAD R8, #0x200000
    LOAD R9, [R8]
