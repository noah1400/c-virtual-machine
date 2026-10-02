; The memory syscalls under paging: a range across a page edge, where the two pages lie the other way round
; in physical memory, is filled, counted, searched, compared, hashed and sorted as one, and a range that runs
; into an unmapped page reports a page fault in R5

.equ PAGE_PRESENT, 0x1
.equ PAGE_WRITE, 0x2
.equ PAGE_EXEC, 0x8
.equ WINDOW, 0x40000000
.equ EDGE, WINDOW + 0xFF0

.text
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
    LOAD R8, second_page
    OR R8, #PAGE_PRESENT | PAGE_WRITE
    STORE R8, [window_table]
    LOAD R8, first_page
    OR R8, #PAGE_PRESENT | PAGE_WRITE
    STORE R8, [window_table + 4]
    LOAD R8, directory
    MTCR PTB, R8

    LOAD R0, #EDGE
    LOAD R5, #'a'
    LOAD R6, #32
    SYSCALL #24
    MOVE R8, R0
    CALL print_int
    LOAD R0, #EDGE - 16
    LOAD R5, #'a'
    LOAD R6, #64
    SYSCALL #27
    MOVE R8, R0
    CALL print_int
    LOAD R0, #EDGE - 16
    LOAD R5, #'a'
    LOAD R6, #64
    SYSCALL #26
    MOVE R8, R0
    CALL print_int
    LOAD R0, #EDGE
    LOAD R5, letters
    LOAD R6, #32
    SYSCALL #25
    MOVE R8, R0
    CALL print_int
    LOAD R0, #EDGE
    LOAD R5, #2166136261
    LOAD R6, #32
    SYSCALL #29
    MOVE R11, R0
    LOAD R0, letters
    LOAD R5, #2166136261
    LOAD R6, #32
    SYSCALL #29
    SUB R0, R11
    MOVE R8, R0
    CALL print_int

    LOAD R9, #WINDOW + 0xFF8
    LOAD R8, #30
    STORE R8, [R9]
    LOAD R8, #-10
    STORE R8, [R9+4]
    LOAD R8, #20
    STORE R8, [R9+8]
    LOAD R8, #-40
    STORE R8, [R9+12]
    LOAD R0, #WINDOW + 0xFF8
    LOAD R6, #4
    SYSCALL #28
    LOAD R10, #4
.show:
    LOAD R8, [R9]
    CALL print_int
    ADD R9, #4
    LOOP R10, .show
    LOAD R8, [first_page]
    CALL print_int

    LOAD R0, #WINDOW + 0x1FF0
    LOAD R5, #0
    LOAD R6, #32
    SYSCALL #27
    MOVE R8, R5
    CALL print_int
    HALT

.include "lib.inc"

.data
    .align 4096
directory:
    .space 4096
low_table:
    .space 4096
window_table:
    .space 4096
first_page:
    .space 4096
second_page:
    .space 4096
letters:
    .ascii "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
