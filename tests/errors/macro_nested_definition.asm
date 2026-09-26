; expect-error: macro definitions cannot be nested
.macro OUTER
.macro INNER
.endm
.endm
