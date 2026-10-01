; With -k, sleeping does not wait, as the script counts instructions instead of time: a program that sleeps
; between looks at the keyboard still gets its keys at once

.equ KEY_STATUS, 0x60
.equ KEY_DATA,   0x61

.text
next:
    IN R9, #KEY_STATUS
    TEST R9, #1
    JNZ key
    TEST R9, #2
    JNZ ended
    LOAD R0, #10
    SYSCALL #31
    JMP next
key:
    IN R8, #KEY_DATA
    CALL print_int
    JMP next
ended:
    SYSCALL #32
    MOVE R10, R0
    LOAD R0, waited_text
    CMP R10, #1000
    JAE report
    LOAD R0, quick_text
report:
    SYSCALL #2
    HALT

.include "lib.inc"

.data
waited_text:
    .asciiz "the sleeps waited\n"
quick_text:
    .asciiz "the sleeps did not wait\n"
