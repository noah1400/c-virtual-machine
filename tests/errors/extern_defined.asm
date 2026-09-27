; expect-error: 'other' is already defined
; asm-args: -c
.extern other
.text
other:
    HALT
