; With -k, keys come from a script that releases them once enough instructions have run

.equ KEY_STATUS, 0x60
.equ KEY_DATA,   0x61

.text
next:
    IN R9, #KEY_STATUS
    TEST R9, #1
    JNZ key
    TEST R9, #2
    JZ next
    LOAD R0, ended_text
    SYSCALL #2
    HALT
key:
    SYSCALL #33
    MOVE R10, R0
    IN R8, #KEY_DATA
    CALL print_int
    MOVE R8, R10
    CALL print_int
    JMP next

.include "lib.inc"

.data
ended_text:
    .asciiz "input ended\n"
