; expect-error: .text section exceeds 16777216 bytes
.text
    .space 0x1000000
    HALT
