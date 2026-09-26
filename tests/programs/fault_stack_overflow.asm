; expect-exit: 1
; expect-stderr: Stack overflow
.text
recurse:
    CALL recurse
