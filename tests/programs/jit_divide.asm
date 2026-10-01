; IDIV and IMOD by -1 in compiled code, where only the lowest integer divided by -1 faults
; vm-args: -j 1

.text
    LOAD R8, vectors
    MTCR IVTB, R8
    LOAD R10, values
.next:
    LOAD R11, [R10]
    MOVE R8, R11
    IDIV R8, #-1
    CALL print_int
    MOVE R8, R11
    IMOD R8, #-1
    CALL print_int
    ADD R10, #4
    CMP R10, values_end
    JNZ .next
    HALT

on_division:
    LOAD R0, division_text
    SYSCALL #2
    ; The saved PC points at the faulting IDIV or IMOD, which takes one word
    LOAD R11, [SP+12]
    ADD R11, #4
    STORE R11, [SP+12]
    IRET

.include "lib.inc"

.data
vectors:
    .space 5 * 4
    .dword on_division
    .space 250 * 4
values:
    .dword 7, -9, 0x80000000
values_end:
division_text:
    .asciiz "Division fault handled\n"
