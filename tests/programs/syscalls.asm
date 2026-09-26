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

    ; -2.25 in 16.16 fixed point
    LOAD R0, #0xFFFD
    SHL R0, #16
    OR R0, #0xC000
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
