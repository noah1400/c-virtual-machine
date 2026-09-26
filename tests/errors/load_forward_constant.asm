; expect-error: constant 100000 needs 32 bits but is defined after this LOAD
.text
    LOAD R0, #LATER
.equ LATER, 100000
