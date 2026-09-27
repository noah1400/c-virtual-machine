; Paging: an alias through the page tables, a page mapped on demand by the page fault handler and
; a write to a read-only page

.equ PAGE_PRESENT, 0x1
.equ PAGE_WRITE, 0x2
.equ PAGE_EXEC, 0x8
.equ FAULT_PRESENT, 0x1
.equ WINDOW, 0x40000000
.equ DEMAND, 0x50000000

.text
    ; Identity map the first megabyte, where code, data and stack live
    LOAD R8, low_table
    LOAD R9, #PAGE_PRESENT | PAGE_WRITE | PAGE_EXEC
    LOAD R10, #256
.identity:
    STORE R9, [R8]
    ADD R8, #4
    ADD R9, #0x1000
    LOOP R10, .identity

    LOAD R8, low_table
    OR R8, #PAGE_PRESENT
    STORE R8, [directory]
    LOAD R8, window_table
    OR R8, #PAGE_PRESENT
    STORE R8, [directory + (WINDOW >> 22) * 4]
    LOAD R8, demand_table
    OR R8, #PAGE_PRESENT
    STORE R8, [directory + (DEMAND >> 22) * 4]

    ; The window page appears writable at WINDOW and read-only one page above
    LOAD R8, window_page
    OR R8, #PAGE_PRESENT | PAGE_WRITE
    STORE R8, [window_table]
    AND R8, #~PAGE_WRITE
    STORE R8, [window_table + 4]

    LOAD R8, vectors
    MTCR IVTB, R8
    LOAD R8, directory
    MTCR PTB, R8

    LOAD R9, #WINDOW
    LOAD R8, #1234
    STORE R8, [R9]
    LOAD R8, [window_page]
    CALL print_int

    LOAD R9, #DEMAND + 0x10
    LOAD R8, #99
    STORE R8, [R9]
    LOAD R8, [R9]
    CALL print_int
    LOAD R8, [spare_page + 0x10]
    CALL print_int

    LOAD R9, #WINDOW + 0x1000
    LOAD R8, [R9]
    CALL print_int
    STORE R8, [R9]
    HALT

; Pages in the demand area get the spare page; any other fault ends the program
on_page_fault:
    MFCR R10, FADDR
    MFCR R11, ECODE
    MOVE R12, R10
    SHR R12, #22
    CMP R12, #DEMAND >> 22
    JNZ .unexpected
    AND R11, #FAULT_PRESENT
    JNZ .unexpected
    SHR R10, #12
    AND R10, #0x3FF
    SHL R10, #2
    ADD R10, demand_table
    LOAD R12, spare_page
    OR R12, #PAGE_PRESENT | PAGE_WRITE
    STORE R12, [R10]
    LOAD R0, mapped_text
    SYSCALL #2
    IRET
.unexpected:
    LOAD R0, fault_text
    SYSCALL #2
    MOVE R8, R11
    CALL print_int
    HALT

.include "lib.inc"

.data
directory:
    .space 4096
low_table:
    .space 4096
window_table:
    .space 4096
demand_table:
    .space 4096
window_page:
    .space 4096
spare_page:
    .space 4096
vectors:
    .space 14 * 4
    .dword on_page_fault
    .space 241 * 4
mapped_text:
    .asciiz "Mapped a page on demand\n"
fault_text:
    .asciiz "Page fault with ECODE "
