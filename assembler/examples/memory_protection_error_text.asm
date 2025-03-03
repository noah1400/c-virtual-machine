; Test for Memory Protection Violations
; Requires the PROTECT instruction to be implemented

.text
    ; Print header
    LOAD R0, header_text
    SYSCALL #2
    LOAD R0, #10
    SYSCALL #0
    SYSCALL #0
    
    ; Allocate a block for testing
    LOAD R0, alloc_text
    SYSCALL #2
    LOAD R12, #64
    ALLOC R10, R12
    
    ; Print the address
    LOAD R0, address_text
    SYSCALL #2
    MOVE R0, R10
    SYSCALL #5
    LOAD R0, #10
    SYSCALL #0
    
    ; Write some data to the block
    LOAD R0, write_text
    SYSCALL #2
    LOAD R12, #65     ; 'A'
    STOREB R12, [R10]
    LOAD R12, #66     ; 'B'
    STOREB R12, [R10+1]
    LOAD R12, #67     ; 'C'
    STOREB R12, [R10+2]
    LOAD R12, #0      ; Null terminator
    STOREB R12, [R10+3]
    LOAD R0, done_text
    SYSCALL #2
    LOAD R0, #10
    SYSCALL #0
    
    ; Read the data (should work)
    LOAD R0, read_text
    SYSCALL #2
    MOVE R0, R10
    SYSCALL #2        ; Print string
    LOAD R0, #10
    SYSCALL #0
    
    ; Protection flags definitions
    ; PROT_NONE = 0, PROT_READ = 1, PROT_WRITE = 2, PROT_EXEC = 4, PROT_ALL = 7
    
    ; Set memory to read-only
    LOAD R0, protect_read_text
    SYSCALL #2
    LOAD R12, #1       ; PROT_READ
    PROTECT R10, R12   ; Set protection
    LOAD R0, done_text
    SYSCALL #2
    LOAD R0, #10
    SYSCALL #0
    
    ; Test reading (should succeed)
    LOAD R0, test_read_text
    SYSCALL #2
    LOADB R0, [R10]
    SYSCALL #0         ; Print as character
    LOAD R0, #10
    SYSCALL #0
    
    ; Test writing (should fail with protection error)
    LOAD R0, test_write_text
    SYSCALL #2
    LOAD R12, #90      ; 'Z'
    STOREB R12, [R10]  ; Should cause protection violation
    
    ; This should not execute if error handling is working
    LOAD R0, no_error_text
    SYSCALL #2
    LOAD R0, #10
    SYSCALL #0
    
    ; Set memory back to all-access
    LOAD R0, protect_all_text
    SYSCALL #2
    LOAD R12, #7       ; PROT_ALL
    PROTECT R10, R12   ; Set protection
    LOAD R0, done_text
    SYSCALL #2
    LOAD R0, #10
    SYSCALL #0
    
    ; Free the block
    LOAD R0, free_text
    SYSCALL #2
    FREE R10
    LOAD R0, done_text
    SYSCALL #2
    LOAD R0, #10
    SYSCALL #0
    
    ; Print completion
    LOAD R0, complete_text
    SYSCALL #2
    LOAD R0, #10
    SYSCALL #0
    
    HALT

.data
header_text:
    .asciiz "Memory Protection Test"
    
alloc_text:
    .asciiz "Allocating memory block... "
    
address_text:
    .asciiz "Allocated address: "
    
write_text:
    .asciiz "Writing data to block... "

read_text:
    .asciiz "Reading data from block: "
    
done_text:
    .asciiz "Done"
    
protect_read_text:
    .asciiz "Setting memory to read-only... "
    
test_read_text:
    .asciiz "Testing read operation (should succeed): "
    
test_write_text:
    .asciiz "Testing write operation (should fail): "
    
no_error_text:
    .asciiz "ERROR: No protection violation detected!"
    
protect_all_text:
    .asciiz "Setting memory back to all-access... "
    
free_text:
    .asciiz "Freeing memory block... "
    
complete_text:
    .asciiz "Test completed"