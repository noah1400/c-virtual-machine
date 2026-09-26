; expect-error: instruction at unaligned address 0x0001, use .align 4
.text
    .byte 1
    HALT
