; expect-error: LEVEL must be at most 3
; asm-args: -D LEVEL=5
.if LEVEL > 3
.error "LEVEL must be at most 3"
.endif
.text
    HALT
