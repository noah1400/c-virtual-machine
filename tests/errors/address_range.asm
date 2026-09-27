; expect-error: address 4294967296 is out of range (0 to 4294967295)
.text
    LOAD R0, [0x100000000]
