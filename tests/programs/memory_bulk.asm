; Syscalls 24 to 29 fill, compare, search, count, sort and hash memory in native code, and report a range
; outside memory with 0 in R0 and the error in R5

.text
    LOAD R0, buffer
    LOAD R5, #'x'
    LOAD R6, #10
    SYSCALL #24
    MOVE R8, R0
    CALL print_int
    LOAD R0, buffer
    SYSCALL #2
    LOAD R0, #'\n'
    SYSCALL #0

    LOAD R0, apple
    LOAD R5, apply
    LOAD R6, #5
    SYSCALL #25
    MOVE R8, R0
    CALL print_int
    LOAD R0, apple
    LOAD R5, apply
    LOAD R6, #4
    SYSCALL #25
    MOVE R8, R0
    CALL print_int

    LOAD R0, hello
    LOAD R5, #'l'
    LOAD R6, #11
    SYSCALL #26
    MOVE R8, R0
    CALL print_int
    LOAD R0, hello
    LOAD R5, #'z'
    LOAD R6, #11
    SYSCALL #26
    MOVE R8, R0
    CALL print_int

    LOAD R0, hello
    LOAD R5, #'o'
    LOAD R6, #11
    SYSCALL #27
    MOVE R8, R0
    CALL print_int

    LOAD R0, numbers
    LOAD R6, #7
    SYSCALL #28
    LOAD R9, numbers
    LOAD R10, #7
show:
    LOAD R8, [R9]
    CALL print_int
    ADD R9, #4
    LOOP R10, show

    ; FNV-1a of "hello" at once, then in two pieces
    LOAD R0, hello
    LOAD R5, #2166136261
    LOAD R6, #5
    SYSCALL #29
    MOVE R8, R0
    CALL print_hex
    LOAD R0, hello
    LOAD R5, #2166136261
    LOAD R6, #2
    SYSCALL #29
    MOVE R5, R0
    LOAD R0, hello + 2
    LOAD R6, #3
    SYSCALL #29
    MOVE R8, R0
    CALL print_hex

    LOAD R0, #0xFFFFFF00
    LOAD R5, #0
    LOAD R6, #0x200
    SYSCALL #27
    MOVE R11, R5
    MOVE R8, R0
    CALL print_int
    MOVE R8, R11
    CALL print_int
    LOAD R0, numbers
    LOAD R6, #0x40000001
    SYSCALL #28
    MOVE R8, R5
    CALL print_int
    HALT

.include "lib.inc"

.data
buffer:
    .space 10
    .byte 0
apple:
    .ascii "apple"
apply:
    .ascii "apply"
hello:
    .ascii "hello world"
    .align 4
numbers:
    .dword 5, -3, 100000, 0, -7, 42, -2147483648
