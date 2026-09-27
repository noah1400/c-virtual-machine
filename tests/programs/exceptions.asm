; Faults whose vector has a handler resume the program instead of stopping it

.text
    LOAD R8, vectors
    MTCR IVTB, R8

    LOAD R8, #10
    LOAD R9, #0
    DIV R8, R9
    CALL print_int

    ; The segmentation fault handler hands the faulting address back in R8
    LOAD R10, #0x7FFFFFF0
    LOAD R8, [R10]
    CALL print_hex
    LOAD R8, [0x7FFFFFF4]
    CALL print_hex

    SYSCALL #999
    LOAD R8, #7
    CALL print_int
    HALT

on_segmentation_fault:
    LOAD R0, segmentation_text
    SYSCALL #2
    MFCR R11, FADDR
    STORE R11, [SP+32]
    CALL skip_faulting_instruction
    IRET

on_division:
    LOAD R0, division_text
    SYSCALL #2
    CALL skip_faulting_instruction
    IRET

on_syscall:
    LOAD R0, syscall_text
    SYSCALL #2
    CALL skip_faulting_instruction
    IRET

; The frame holds R0 to R15 above the return address; the saved PC points at the faulting
; instruction, which is 8 bytes long when bit 23 of its first word is set
skip_faulting_instruction:
    LOAD R11, [SP+16]
    LOAD R12, [R11]
    AND R12, #0x800000
    JZ .short
    ADD R11, #4
.short:
    ADD R11, #4
    STORE R11, [SP+16]
    RET

.include "lib.inc"

.data
vectors:
    .space 2 * 4
    .dword on_segmentation_fault
    .space 2 * 4
    .dword on_division
    .space 4
    .dword on_syscall
    .space 248 * 4
segmentation_text:
    .asciiz "Segmentation fault handled\n"
division_text:
    .asciiz "Division by zero handled\n"
syscall_text:
    .asciiz "Invalid system call handled\n"
