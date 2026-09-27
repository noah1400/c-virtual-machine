; expect-error: the entry point of an object file must be a label in its code
; asm-args: -c
.entry value
.text
    HALT
.data
value:
    .dword 0
