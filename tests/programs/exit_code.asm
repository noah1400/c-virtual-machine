; Programs report their exit code through syscall 30
; expect-exit: 3

.text
    LOAD R0, #3
    SYSCALL #30
    HALT
