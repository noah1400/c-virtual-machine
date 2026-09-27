; expect-error: floating-point constants cannot be combined with operators
.text
    LOAD R0, #1.5 + 1
