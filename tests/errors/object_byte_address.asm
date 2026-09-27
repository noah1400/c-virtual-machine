; expect-error: only .dword can hold an address in an object file
; asm-args: -c
.data
here:
    .byte here
