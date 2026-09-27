; expect-error: the .rept count must be a constant defined before it
.rept COUNT
    NOP
.endr
.equ COUNT, 2
