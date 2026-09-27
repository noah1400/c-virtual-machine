; Number formatting, random numbers and invalid syscalls
; expect-exit: 1
; expect-stderr: Invalid system call: 99

.text
    LOAD R0, #255
    LOAD R5, #16
    SYSCALL #6
    LOAD R0, #'\n'
    SYSCALL #0
    LOAD R0, #35
    LOAD R5, #36
    SYSCALL #6
    LOAD R0, #'\n'
    SYSCALL #0

    ; -2.25 as a single-precision float
    LOAD R0, #0xC0100000
    SYSCALL #7
    LOAD R0, #'\n'
    SYSCALL #0

    ; The same seed gives the same sequence
    LOAD R0, #7
    SYSCALL #41
    LOAD R0, #1000
    SYSCALL #40
    MOVE R8, R0
    LOAD R0, #7
    SYSCALL #41
    LOAD R0, #1000
    SYSCALL #40
    SUB R8, R0
    CALL print_int

    SYSCALL #99
    HALT

.include "lib.inc"
