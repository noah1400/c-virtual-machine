; expect-error: the .space size changes when the labels it uses move
.text
start:
    LOAD R0, #later
here:
    .space here - start
    HALT
.equ later, 100000
