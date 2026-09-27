; expect-error: the line number must not depend on symbols defined later
.text
    .loc "main.c", LINE
    HALT
.equ LINE, 3
