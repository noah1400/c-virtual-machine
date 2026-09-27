; expect-error: an address of an object file cannot be used here, only a constant
; asm-args: -c
.data
start:
    .space start
