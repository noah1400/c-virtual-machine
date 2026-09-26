; expect-error: the .space size must not depend on symbols defined later
.data
    .space later
.equ later, 4
