; expect-error: index offset 3000 is out of range (-2048 to 2047)
.text
    LOAD R0, [R5 + 3000]
