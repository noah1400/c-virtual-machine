; expect-error: PAIR expects 1 to 2 arguments
.macro PAIR a, b=R5
    MOVE \a, \b
.endm
.text
    PAIR
