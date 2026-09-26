; expect-error: entry point 0x6 is not instruction aligned
.entry start + 2
.text
    NOP
start:
    HALT
