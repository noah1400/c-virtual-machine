; -T also takes a date alone, which stands for its midnight
; vm-args: -T 1970-01-02
    SYSCALL #36
    SYSCALL #6
    LOAD R0, #'\n'
    SYSCALL #0
    HALT
