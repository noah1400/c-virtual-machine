; A kernel enters user mode through IRET, serves software interrupts on its own stack and stops
; the program when it tries a privileged instruction

.equ SERVICE, 0x20
.equ SUPERVISOR, 0x40

.text
kernel:
    LOAD R8, vectors
    MTCR IVTB, R8
    MFCR R8, SHI
    SUB R8, #0x8000
    MTCR KSP, R8

    ; IRET pops R0 to R15; a saved SR of 0 means user mode with interrupts off
    LOAD R9, #0
    LOAD R10, #12
.frame:
    PUSH R9
    LOOP R10, .frame
    PUSH user_main
    MFCR R8, SHI
    PUSH R8
    PUSH R9
    PUSH R9
    IRET

; R5 = 0 prints the string at R0, R5 = 1 prints the number in R0
on_service:
    CMP R5, #1
    JZ .number
    SYSCALL #2
    IRET
.number:
    SYSCALL #1
    LOAD R0, #'\n'
    SYSCALL #0
    IRET

on_privilege_violation:
    LOAD R0, violation_text
    SYSCALL #2
    HALT

user_main:
    LOAD R0, hello_text
    LOAD R5, #0
    INT #SERVICE
    MOVE R0, SP
    LOAD R5, #1
    INT #SERVICE

    ; Writing SR cannot grant supervisor mode
    LOAD R8, #SUPERVISOR
    PUSH R8
    POPF
    PUSHF
    POP R0
    AND R0, #SUPERVISOR
    LOAD R5, #1
    INT #SERVICE

    ; Interrupt frames went to the kernel stack, so SP is where it was
    MOVE R0, SP
    INT #SERVICE
    HALT

.data
vectors:
    .space 13 * 4
    .dword on_privilege_violation
    .space (SERVICE - 14) * 4
    .dword on_service
    .space (255 - SERVICE) * 4
hello_text:
    .asciiz "Hello from user mode\n"
violation_text:
    .asciiz "HALT stopped by the kernel\n"
