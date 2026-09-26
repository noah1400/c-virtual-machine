; expect-error: cannot resolve the value of 'A'
.equ A, B + 1
.equ B, A + 1
