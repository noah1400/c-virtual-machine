; Heap allocation, coalescing, protection and block copies

.text
    ; Freed neighbours merge back into one block large enough for 12000 bytes
    ALLOC R8, #4000
    ALLOC R9, #4000
    ALLOC R10, #4000
    FREE R9
    FREE R8
    FREE R10
    ALLOC R8, #12000
    CALL print_hex

    ; Allocations are zero-filled
    LOAD R8, [R8+100]
    CALL print_int

    ; MEMSET and MEMCPY with immediate and register sizes
    ALLOC R11, #16
    LOAD R12, #'x'
    MEMSET R11, R12, #3
    LOAD R13, #4
    LOAD R14, buffer
    MEMCPY R14, R11, R13
    LOAD R0, buffer
    SYSCALL #2
    LOAD R0, #'\n'
    SYSCALL #0

    ; A read-only block can still be read
    PROTECT R11, #1
    LOADB R8, [R11]
    CALL print_int

    ; Heap syscalls report failures in R5 instead of stopping the program
    LOAD R0, #0x7FFFFFFF
    SYSCALL #20
    MOVE R8, R5
    CALL print_int
    LOAD R0, #0x1000
    SYSCALL #21
    MOVE R8, R5
    CALL print_int

    SYSCALL #23
    MOVE R8, R6
    CALL print_int
    MOVE R8, R7
    CALL print_int
    HALT

.include "lib.inc"

.data
buffer:
    .space 8
