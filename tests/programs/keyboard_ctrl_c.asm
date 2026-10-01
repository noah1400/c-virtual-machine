; With a vector in port 0x63, Ctrl-C requests that interrupt instead of stopping the machine. A Ctrl-C byte
; from a key script does the same when the program looks at the keyboard, and the other keys still wait.

.equ KEY_STATUS, 0x60
.equ KEY_DATA,   0x61
.equ KEY_CTRL_C, 0x63
.equ VECTOR,     0x23

.text
    LOAD R8, vectors
    MTCR IVTB, R8
    LOAD R8, #VECTOR
    OUT #KEY_CTRL_C, R8
    IN R8, #KEY_CTRL_C
    CALL print_int
    STI
next:
    IN R9, #KEY_STATUS
    TEST R9, #1
    JNZ key
    TEST R9, #2
    JZ next
    LOAD R0, count_text
    SYSCALL #2
    LOAD R8, [breaks]
    CALL print_int
    HALT
key:
    IN R8, #KEY_DATA
    CALL print_int
    JMP next

on_ctrl_c:
    LOAD R8, [breaks]
    INC R8
    STORE R8, [breaks]
    LOAD R0, break_text
    SYSCALL #2
    IRET

.include "lib.inc"

.data
vectors:
    .space VECTOR * 4
    .dword on_ctrl_c
    .space (255 - VECTOR) * 4
breaks:
    .dword 0
break_text:
    .asciiz "^C\n"
count_text:
    .asciiz "Ctrl-C: "
