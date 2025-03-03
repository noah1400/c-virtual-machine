; Test for Allocation Size Error
; Attempts to allocate more memory than is available

.text
    ; Print header
    LOAD R0, header_text
    SYSCALL #2
    LOAD R0, #10
    SYSCALL #0
    SYSCALL #0
    
    ; Attempt to allocate too much memory
    LOAD R0, test1_text
    SYSCALL #2
    
    ; Try to allocate 32KB (heap segment is only 16KB)
    LOAD R12, #32768
    
    ; Print the size we're trying to allocate
    LOAD R0, size_text
    SYSCALL #2
    MOVE R0, R12
    SYSCALL #1
    LOAD R0, #10
    SYSCALL #0
    
    ; Attempt to allocate - should cause ERROR_MEMORY_ALLOCATION
    LOAD R0, attempt_text
    SYSCALL #2
    ALLOC R10, R12
    
    ; This should not execute if error handling is working
    LOAD R0, no_error_text
    SYSCALL #2
    LOAD R0, #10
    SYSCALL #0
    
    ; Print completion message (shouldn't reach here)
    LOAD R0, complete_text
    SYSCALL #2
    LOAD R0, #10
    SYSCALL #0
    
    HALT

.data
header_text:
    .asciiz "Allocation Size Error Test"
    
test1_text:
    .asciiz "Test 1: Allocating more memory than available"
    
size_text:
    .asciiz "Attempting to allocate bytes: "
    
attempt_text:
    .asciiz "Attempting allocation... "
    
no_error_text:
    .asciiz "ERROR: No error was detected!"
    
complete_text:
    .asciiz "Test completed"