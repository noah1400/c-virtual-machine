; Keys come from stdin one at a time, with the arrow and editing keys decoded, other escape sequences dropped
; and an escape before another one taken as the Escape key

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
    IN R8, #KEY_DATA
    CALL print_int
    JMP next

.include "lib.inc"

.data
ended_text:
    .asciiz "input ended\n"
