; Test for Double Free Error
; Attempts to free the same memory block twice

.text
    ; Print header
    LOAD R0, header_text
    SYSCALL #2
    LOAD R0, #10
    SYSCALL #0
    SYSCALL #0
    
    ; Allocate a memory block
    LOAD R0, alloc_text
    SYSCALL #2
    LOAD R12, #64    ; Size - 64 bytes
    ALLOC R10, R12   ; Allocate and store in R10
    
    ; Print the address
    LOAD R0, address_text
    SYSCALL #2
    MOVE R0, R10
    SYSCALL #5
    LOAD R0, #10
    SYSCALL #0
    
    ; Write some data to the block to prove it's valid
    LOAD R0, write_text
    SYSCALL #2
    LOAD R0, #65     ; 'A'
    STOREB R0, [R10]
    LOAD R0, #0      ; Null terminator
    STOREB R0, [R10+1]
    LOAD R0, success_text
    SYSCALL #2
    LOAD R0, #10
    SYSCALL #0
    
    ; Free the block first time - should succeed
    LOAD R0, free1_text
    SYSCALL #2
    FREE R10
    LOAD R0, success_text
    SYSCALL #2
    LOAD R0, #10
    SYSCALL #0
    
    ; Free the block second time - should fail with DOUBLE FREE
    LOAD R0, free2_text
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
    .asciiz "Double Free Error Test"
    
alloc_text:
    .asciiz "Allocating memory block... "
    
address_text:
    .asciiz "Allocated address: "
    
write_text: 
    .asciiz "Writing data to block... "
    
success_text:
    .asciiz "Success"
    
free1_text:
    .asciiz "Freeing block (first time)... "
    
free2_text:
    .asciiz "Freeing block (second time)... "
    
no_error_text:
    .asciiz "ERROR: No error was detected for double free!"
    
complete_text:
    .asciiz "Test completed"