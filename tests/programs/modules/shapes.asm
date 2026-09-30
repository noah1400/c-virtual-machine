; area(w, h) the way a compiler might write it, with the code it copied from the functions it calls between
; .inline and .endinline
    .global area

.text
    .loc "shapes.c", 9, "int area(int w, int h) {"
area:
    ENTER 0
    .loc "shapes.c", 10, "    return scale(w, h);"
    LOAD R0, [BP+8]
    LOAD R5, [BP+12]
    .inline "shapes.c", 10
    .inline "shapes.c", 5
    .loc "shapes.c", 2, "    return a / b;"
    MOVE R6, R0
    DIV R6, R5
    .endinline
    .loc "shapes.c", 5, "    return ratio(a, b) * a;"
    MUL R6, R0
    .endinline
    .loc "shapes.c", 10, "    return scale(w, h);"
    MOVE R0, R6
    LEAVE
    RET #8
