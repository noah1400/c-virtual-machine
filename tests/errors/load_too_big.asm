; expect-error: constant 4294967296 does not fit in 32 bits
.text
    LOAD R0, #0x100000000
