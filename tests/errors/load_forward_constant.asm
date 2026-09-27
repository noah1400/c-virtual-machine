; expect-error: immediate 100000 needs an extension word but depends on a symbol defined later
.text
    LOAD R0, #LATER
.equ LATER, 100000
