; Minimal Memory Allocation Test
; The simplest possible test to verify memory allocation

.text
    ; Print header
    LOAD R0, header_text
    SYSCALL #2        ; Print string
    LOAD R0, #10      ; Newline
    SYSCALL #0        ; Print character

    ; Allocate memory and print address
    LOAD R0, allocating_text
    SYSCALL #2
    
    ; Try direct immediate allocation (simplest case)
    ALLOC R10, #100
    
    LOAD R0, address_text
    SYSCALL #2
    MOVE R0, R10       ; Move address to R0 for printing
    SYSCALL #5        ; Print hex
    LOAD R0, #10
    SYSCALL #0
    
    ; Try writing to the allocated memory
    LOAD R0, writing_text
    SYSCALL #2
    LOAD R1, #65      ; ASCII 'A'
    STOREB R1, [R10]   ; Store at beginning
    LOAD R1, #66      ; ASCII 'B'
    STOREB R1, [R10+1] ; Store at second byte
    LOAD R1, #67      ; ASCII 'C'
    STOREB R1, [R10+2] ; Store at third byte
    LOAD R1, #0       ; Null terminator
    STOREB R1, [R10+3] ; Store at fourth byte
    LOAD R0, done_text
    SYSCALL #2
    LOAD R0, #10
    SYSCALL #0

    ; Try reading the string we just wrote
    LOAD R0, reading_text
    SYSCALL #2
    MOVE R0, R10       ; Address of string
    SYSCALL #2        ; Print string (syscall 2 expects null-terminated string)
    LOAD R0, #10
    SYSCALL #0

    ; Free the memory
    LOAD R0, freeing_text
    SYSCALL #2
    FREE R10
    LOAD R0, done_text
    SYSCALL #2
    LOAD R0, #10
    SYSCALL #0

    ; End program
    LOAD R0, complete_text
    SYSCALL #2
    LOAD R0, #10
    SYSCALL #0
    HALT

.data
header_text:
    .asciiz "Minimal Memory Allocation Test"

allocating_text:
    .asciiz "Allocating 100 bytes... "

address_text:
    .asciiz "Address: "

writing_text:
    .asciiz "Writing string to memory... "

done_text:
    .asciiz "Done"

reading_text:
    .asciiz "Reading string from memory: "

freeing_text:
    .asciiz "Freeing memory... "

complete_text:
    .asciiz "Test completed successfully!"