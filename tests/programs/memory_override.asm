; vm -m wins over the memory that a program asks for
; asm-args: -m 2048
; vm-args: -m 512
    SYSCALL #23
    SYSCALL #1
    LOAD R0, #'\n'
    SYSCALL #0
    HALT
