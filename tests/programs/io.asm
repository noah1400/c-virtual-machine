; Console ports and the input syscalls
; expect-stderr: to stderr

.text
    ; Echo two characters through the console data port
    IN R8, #0
    OUT #0, R8
    IN R8, #0
    OUT #0, R8

    ; The rest of the line, then a line that is too long for the buffer
    LOAD R0, buffer
    LOAD R5, #16
    SYSCALL #4
    MOVE R8, R0
    CALL print_int
    LOAD R0, buffer
    SYSCALL #2
    LOAD R0, #'\n'
    SYSCALL #0

    LOAD R0, buffer
    LOAD R5, #4
    SYSCALL #4
    LOAD R0, buffer
    SYSCALL #2
    LOAD R0, #'\n'
    SYSCALL #0

    ; Drain the input, then getchar reports the end with status 11 in R5
drain:
    SYSCALL #3
    CMP R5, #0
    JZ drain
    MOVE R8, R5
    CALL print_int

    ; Port 1 writes to stderr through a register-selected port
    LOAD R9, #1
    LOAD R10, message
print_error:
    LOADB R8, [R10]
    CMP R8, #0
    JZ done
    OUT R9, R8
    INC R10
    JMP print_error
done:
    HALT

.include "lib.inc"

.data
buffer:
    .space 16
message:
    .asciiz "to stderr\n"
