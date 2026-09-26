; expect-error: PAIR expects 2 arguments
.macro PAIR a, b
    MOVE \a, \b
.endm
.text
    PAIR R1
