; A small kernel: paging keeps a user program away from kernel memory, a software interrupt
; serves its requests and a page fault ends it

.equ PAGE_SIZE, 4096
.equ PAGE_PRESENT, 0x1
.equ PAGE_WRITE, 0x2
.equ PAGE_USER, 0x4
.equ PAGE_EXEC, 0x8
.equ PRIVILEGE_VIOLATION, 13
.equ PAGE_FAULT, 14
.equ SERVICE, 0x30

.text
kernel:
    ; Every page of the first megabyte belongs to the kernel
    LOAD R8, page_table
    LOAD R9, #PAGE_PRESENT | PAGE_WRITE | PAGE_EXEC
    LOAD R10, #256
.map:
    STORE R9, [R8]
    ADD R8, #4
    ADD R9, #PAGE_SIZE
    LOOP R10, .map
    LOAD R8, page_table
    OR R8, #PAGE_PRESENT
    STORE R8, [page_directory]

    ; The user program may run its code and use its data, nothing else
    LOAD R8, user_code
    LOAD R9, user_code_end
    LOAD R10, #PAGE_PRESENT | PAGE_USER | PAGE_EXEC
    CALL set_page_flags
    LOAD R8, user_data
    LOAD R9, user_data_end
    LOAD R10, #PAGE_PRESENT | PAGE_USER | PAGE_WRITE
    CALL set_page_flags

    LOAD R8, vectors
    MTCR IVTB, R8
    MOVE R8, SP
    MTCR KSP, R8
    ; The user stack lies far from the kernel stack, so the stack bounds are opened up
    LOAD R8, #0
    MTCR SLO, R8
    LOAD R8, #-1
    MTCR SHI, R8
    LOAD R8, page_directory
    MTCR PTB, R8

    ; IRET pops R0 to R15; a saved SR of 0 means user mode
    LOAD R9, #0
    LOAD R10, #12
.frame:
    PUSH R9
    LOOP R10, .frame
    PUSH user_main
    PUSH user_stack_top
    PUSH R9
    PUSH R9
    IRET

; Gives the pages from R8 up to R9 the flags in R10
set_page_flags:
    SHR R8, #12
    SHR R9, #12
.page:
    CMP R8, R9
    JAE .done
    MOVE R11, R8
    SHL R11, #12
    OR R11, R10
    MOVE R12, R8
    SHL R12, #2
    ADD R12, page_table
    STORE R11, [R12]
    INC R8
    JMP .page
.done:
    RET

; R5 = 0 prints the string at R0, R5 = 1 ends the program
on_service:
    CMP R5, #1
    JZ .exit
    SYSCALL #2
    IRET
.exit:
    LOAD R0, exit_text
    SYSCALL #2
    HALT

on_page_fault:
    LOAD R0, page_fault_text
    SYSCALL #2
    MFCR R0, FADDR
    SYSCALL #5
    LOAD R0, ecode_text
    SYSCALL #2
    MFCR R0, ECODE
    SYSCALL #1
    LOAD R0, #'\n'
    SYSCALL #0
    HALT

on_privilege_violation:
    LOAD R0, privilege_text
    SYSCALL #2
    HALT

; The user program, in pages of its own
.align PAGE_SIZE
user_code:
user_main:
    LOAD R0, hello_text
    LOAD R5, #0
    INT #SERVICE
    LOAD R8, [secret]
    LOAD R5, #1
    INT #SERVICE
.align PAGE_SIZE
user_code_end:

.data
page_directory:
    .space PAGE_SIZE
page_table:
    .space PAGE_SIZE
vectors:
    .space PRIVILEGE_VIOLATION * 4
    .dword on_privilege_violation
    .dword on_page_fault
    .space (SERVICE - PAGE_FAULT - 1) * 4
    .dword on_service
    .space (255 - SERVICE) * 4
secret:
    .dword 42
page_fault_text:
    .asciiz "Page fault at "
ecode_text:
    .asciiz ", ECODE "
exit_text:
    .asciiz "User program finished\n"
privilege_text:
    .asciiz "Privilege violation\n"

.align PAGE_SIZE
user_data:
hello_text:
    .asciiz "Hello from user mode\n"
    .align 4
user_stack:
    .space 1024
user_stack_top:
.align PAGE_SIZE
user_data_end:
