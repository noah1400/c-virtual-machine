; CPUID vendor string and memory layout

.text
    LOAD R0, #0
    CPUID
    STORE R5, [vendor]
    STORE R6, [vendor+4]
    MOVE R8, R0
    CALL print_int
    LOAD R0, vendor
    SYSCALL #2
    LOAD R0, #'\n'
    SYSCALL #0

    LOAD R0, #2
    CPUID
    MOVE R10, R5
    MOVE R11, R6
    MOVE R8, R0
    CALL print_int
    MOVE R8, R10
    CALL print_hex
    MOVE R8, R11
    CALL print_hex
    HALT

.include "lib.inc"

.data
vendor:
    .space 12
