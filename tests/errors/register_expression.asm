; expect-error: register R1 cannot be used in an expression
.text
    LOAD R0, R1 + 4
