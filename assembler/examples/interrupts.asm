; Installs a software interrupt handler and shows that IRET restores the registers

.equ VECTOR, 0x10

.text
main:
    ; IVTB points at a table with one handler address per vector
    LOAD R0, vectors
    MTCR IVTB, R0

    LOAD R0, banner
    SYSCALL #2

    LOAD R8, #0x55
    LOAD R9, #0x66
    INT #VECTOR

    LOAD R0, after_text
    SYSCALL #2
    MOVE R0, R8
    SYSCALL #5
    LOAD R0, #' '
    SYSCALL #0
    MOVE R0, R9
    SYSCALL #5
    LOAD R0, #'\n'
    SYSCALL #0
    HALT

handler:
    LOAD R0, handler_text
    SYSCALL #2
    LOAD R8, #0xAA
    LOAD R9, #0xBB
    IRET

.data
vectors:
    .space VECTOR * 4
    .dword handler
    .space (255 - VECTOR) * 4
banner:
    .asciiz "Raising interrupt 0x10 with R8=0x55 R9=0x66\n"
handler_text:
    .asciiz "Inside the handler, overwriting R8 and R9\n"
after_text:
    .asciiz "Back from the handler: "
