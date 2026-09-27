; expect-error: an address in an object file cannot be used with '*'
; asm-args: -c
.data
here:
    .dword here * 2
