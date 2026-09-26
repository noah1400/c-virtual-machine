; expect-exit: 1
; expect-stderr: No device on I/O port 0x0033
.text
    OUT #0x33, R0
