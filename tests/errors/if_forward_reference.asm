; expect-error: a .if condition must not depend on symbols defined later
.if LATER
.endif
.equ LATER, 1
