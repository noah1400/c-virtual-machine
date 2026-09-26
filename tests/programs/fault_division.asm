; expect-exit: 1
; expect-stderr: Division by zero
.text
    LOAD R8, #1
    DIV R8, #0
    HALT
