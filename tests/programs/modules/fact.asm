; fact(n) the way a compiler might write it, with .loc naming the lines of fact.c
    .global fact

.text
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
    LEAVE
    RET
