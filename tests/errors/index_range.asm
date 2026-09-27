; expect-error: index offset 2147483648 is out of range (-2147483648 to 2147483647)
.text
    LOAD R0, [R5 + 0x80000000]
