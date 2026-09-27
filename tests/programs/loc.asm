; .loc names the line of another source, such as a C file, that the code after it came from, so faults
; and backtraces point there instead of at the assembly
; expect-exit: 1

.text
main:
    PUSH #3
    CALL fact
    HALT

    .loc "fact.c", 1, "int fact(int n) {"
fact:
    ENTER 0
    .loc "fact.c", 2, "    if (n <= 1)"
    LOAD R0, [BP+8]
    CMP R0, #1
    JG .recurse
    .loc "fact.c", 3, "        return 1 / (n - 1);"
    LOAD R5, #1
    SUB R0, #1
    DIV R5, R0
    MOVE R0, R5
    LEAVE
    RET
    .loc "fact.c", 4, "    return n * fact(n - 1);"
.recurse:
    SUB R0, #1
    PUSH R0
    CALL fact
    ADD SP, #4
    LOAD R5, [BP+8]
    MUL R0, R5
    .loc "fact.c", 5
    LEAVE
    RET
