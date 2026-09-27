; expect-error: immediate 4294967296 is out of range (-2147483648 to 4294967295)
.text
    ADD R0, #0x100000000
