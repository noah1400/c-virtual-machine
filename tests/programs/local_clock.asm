; Syscall 36 gives the local date and time in seconds since 1970, which vm -T holds still
; vm-args: -T 2026-10-01T09:30:00
    SYSCALL #36
    SYSCALL #6
    LOAD R0, #'\n'
    SYSCALL #0
    HALT
