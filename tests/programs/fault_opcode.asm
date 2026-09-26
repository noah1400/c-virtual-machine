; expect-exit: 1
; expect-stderr: Invalid opcode: 0xFF
.text
    .dword 0xFF000000
