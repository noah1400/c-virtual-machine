; A backtrace lists the calls and interrupts that led to where execution stands
; expect-exit: 1

.text
main:
    CALL outer
    HALT

outer:
    LOAD R8, vectors
    MTCR IVTB, R8
    INT #0x20
    RET

on_interrupt:
    CALL divide
    IRET

divide:
    LOAD R9, #0
    DIV R8, R9
    RET

.data
vectors:
    .space 0x20 * 4
    .dword on_interrupt
