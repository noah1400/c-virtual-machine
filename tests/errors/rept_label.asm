; expect-error: a .rept line cannot have a label
start: .rept 2
    NOP
.endr
