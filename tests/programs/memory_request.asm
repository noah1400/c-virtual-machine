; vmasm -m records in the binary how much memory the program asks for, which vm then gives it
; asm-args: -m 2048
    SYSCALL #23
    SYSCALL #1
    LOAD R0, #'\n'
    SYSCALL #0
    HALT
