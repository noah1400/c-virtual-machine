; Prints the first 25 Fibonacci numbers, one per line

.equ COUNT, 25

.text
    LOAD R8, #0             ; current
    LOAD R9, #1             ; next
    LOAD R10, #COUNT

next_number:
    MOVE R0, R8
    SYSCALL #1
    LOAD R0, #'\n'
    SYSCALL #0

    MOVE R11, R8
    ADD R11, R9
    MOVE R8, R9
    MOVE R9, R11
    LOOP R10, next_number

    HALT
