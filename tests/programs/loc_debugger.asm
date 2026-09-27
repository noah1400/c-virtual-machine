; next runs the whole line that .loc named, which the debugger shows as where the code came from and
; takes as a location

.text
main:
    PUSH #3
    CALL count
    HALT

    .loc "count.c", 1, "int count(int n) {"
count:
    ENTER 4
    .loc "count.c", 2, "    int i = 0;"
    LOAD R5, #0
    STORE R5, [BP-4]
    .loc "count.c", 3, "    while (i < n)"
    JMP .test
    .loc "count.c", 4, "        i++;"
.body:
    LOAD R5, [BP-4]
    INC R5
    STORE R5, [BP-4]
    .loc "count.c", 3, "    while (i < n)"
.test:
    LOAD R5, [BP-4]
    LOAD R6, [BP+8]
    CMP R5, R6
    JL .body
    .loc "count.c", 5, "    return i;"
    MOVE R0, R5
    LEAVE
    RET
