; expect-error: .text section overflows its 16384 byte segment
.text
    .space 0x4000
    HALT
