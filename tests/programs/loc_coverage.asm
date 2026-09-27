; A line that became several instructions, such as a macro call or a line of C named by .loc, ran as often
; as the instruction of it that ran most
; vm-args: -c -

.macro TWICE register
    INC \register
    INC \register
.endm

.text
main:
    LOAD R8, #0
    TWICE R8
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
    .loc "count.c", 5, "    if (i != n)"
    CMP R5, R6
    JE .done
    .loc "count.c", 6, "        return -1;"
    LOAD R0, #-1
    LEAVE
    RET
    .loc "count.c", 7, "    return i;"
.done:
    MOVE R0, R5
    .loc "count.c", 8, "}"
    LEAVE
    RET
