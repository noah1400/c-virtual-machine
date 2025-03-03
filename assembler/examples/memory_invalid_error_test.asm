; Test for Invalid Address Error
; Attempts to free an address that is not in the heap segment

.text
    ; Print header
    LOAD R0, header_text
    SYSCALL #2
    LOAD R0, #10
    SYSCALL #0
    SYSCALL #0

    ; Attempt to free an address in code segment
    LOAD R0, test1_text
    SYSCALL #2
    
    ; Load an obviously non-heap address (code segment)
    LOAD R10, #0x1000
    
    ; Print the address we're trying to free
    LOAD R0, address_text
    SYSCALL #2
    MOVE R0, R10
    SYSCALL #5
    LOAD R0, #10
    SYSCALL #0
    
    ; Attempt to free - should cause ERROR_INVALID_ADDRESS
    LOAD R0, attempt_text
    SYSCALL #2
    FREE R10
    
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
    .asciiz "Invalid Address Error Test"
    
test1_text:
    .asciiz "Test 1: Freeing a non-heap address"
    
address_text:
    .asciiz "Address to free: "
    
attempt_text:
    .asciiz "Attempting to free non-heap address... "
    
no_error_text:
    .asciiz "ERROR: No error was detected!"
    
complete_text:
    .asciiz "Test completed"