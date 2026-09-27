; The keyboard requests its interrupt while keys wait, and the handler takes them until 'q' arrives

.equ KEY_STATUS, 0x60
.equ KEY_DATA,   0x61
.equ KEY_VECTOR, 0x62
.equ VECTOR,     0x21

.text
    LOAD R8, vectors
    MTCR IVTB, R8
    LOAD R8, #VECTOR
    OUT #KEY_VECTOR, R8
    STI
wait:
    LOAD R9, [quit]
    CMP R9, #0
    JZ wait

    CLI
    LOAD R0, count_text
    SYSCALL #2
    LOAD R8, [interrupts]
    CALL print_int
    HALT

on_key:
    LOAD R8, [interrupts]
    INC R8
    STORE R8, [interrupts]
.next:
    IN R9, #KEY_STATUS
    TEST R9, #1
    JZ .done
    IN R0, #KEY_DATA
    SYSCALL #0
    CMP R0, #'q'
    JNE .next
    STORE R0, [quit]
    JMP .next
.done:
    IRET

.include "lib.inc"

.data
vectors:
    .space VECTOR * 4
    .dword on_key
    .space (255 - VECTOR) * 4
interrupts:
    .dword 0
quit:
    .dword 0
count_text:
    .asciiz "\ninterrupts: "
