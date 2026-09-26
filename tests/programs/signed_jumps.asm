; Prints 1 or 0 for JL JGE JLE JG JAE JB JE JNE JN after comparing each pair

.text
    LOAD R8, #3
    NEG R8
    LOAD R9, #5
    CALL compare
    LOAD R8, #5
    LOAD R9, #3
    NEG R9
    CALL compare
    LOAD R8, #4
    LOAD R9, #4
    CALL compare
    ; INT_MIN - 1 overflows, which JN gets wrong and JL gets right
    LOAD R8, #1
    SHL R8, #31
    LOAD R9, #1
    CALL compare
    HALT

compare:
    CMP R8, R9
    LOAD R0, #'1'
    JL .l
    LOAD R0, #'0'
.l: SYSCALL #0
    LOAD R0, #'1'
    JGE .ge
    LOAD R0, #'0'
.ge: SYSCALL #0
    LOAD R0, #'1'
    JLE .le
    LOAD R0, #'0'
.le: SYSCALL #0
    LOAD R0, #'1'
    JG .g
    LOAD R0, #'0'
.g: SYSCALL #0
    LOAD R0, #'1'
    JAE .ae
    LOAD R0, #'0'
.ae: SYSCALL #0
    LOAD R0, #'1'
    JB .b
    LOAD R0, #'0'
.b: SYSCALL #0
    LOAD R0, #'1'
    JE .e
    LOAD R0, #'0'
.e: SYSCALL #0
    LOAD R0, #'1'
    JNE .ne
    LOAD R0, #'0'
.ne: SYSCALL #0
    LOAD R0, #'1'
    JN .n
    LOAD R0, #'0'
.n: SYSCALL #0
    LOAD R0, #'\n'
    SYSCALL #0
    RET
