; expect-exit: 1
; expect-stderr: Unhandled interrupt: 5
.text
    INT #5
