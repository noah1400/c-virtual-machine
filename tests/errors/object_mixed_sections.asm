; expect-error: addresses from different sections or symbols cannot be combined
; asm-args: -c
.text
code:
    HALT
.data
value:
    .dword code + value
