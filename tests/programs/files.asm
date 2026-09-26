; Writing, appending, reading and seeking host files, plus console handles and errors

.text
    LOAD R0, filename
    LOAD R5, #1                 ; create or truncate for writing
    SYSCALL #10
    MOVE R10, R0
    LOAD R5, text
    LOAD R6, #text_end - text
    SYSCALL #13
    MOVE R8, R0
    CALL print_int
    MOVE R0, R10
    SYSCALL #11

    LOAD R0, filename
    LOAD R5, #2                 ; append
    SYSCALL #10
    MOVE R10, R0
    LOAD R5, more
    LOAD R6, #more_end - more
    SYSCALL #13
    MOVE R0, R10
    SYSCALL #11

    LOAD R0, filename
    LOAD R5, #0                 ; read
    SYSCALL #10
    MOVE R10, R0
    LOAD R5, buffer
    LOAD R6, #63
    SYSCALL #12
    MOVE R8, R0
    CALL print_int
    LOAD R0, buffer
    SYSCALL #2

    MOVE R0, R10
    LOAD R5, #6
    LOAD R6, #0                 ; from the start of the file
    SYSCALL #14
    MOVE R8, R0
    CALL print_int
    MOVE R0, R10
    LOAD R5, word
    LOAD R6, #4
    SYSCALL #12
    LOAD R0, word
    SYSCALL #2
    LOAD R0, #'\n'
    SYSCALL #0
    MOVE R0, R10
    SYSCALL #11

    ; Handle 1 is stdout
    LOAD R0, #1
    LOAD R5, text
    LOAD R6, #6
    SYSCALL #13
    LOAD R0, #'\n'
    SYSCALL #0

    ; Failures come back as status 11 in R5
    LOAD R0, missing
    LOAD R5, #0
    SYSCALL #10
    MOVE R8, R5
    CALL print_int
    LOAD R0, #99
    SYSCALL #11
    MOVE R8, R5
    CALL print_int
    HALT

.include "lib.inc"

.data
filename:
    .asciiz "files_test.txt"
missing:
    .asciiz "no/such/dir/file.txt"
text:
    .ascii "hello file\n"
text_end:
more:
    .ascii "second line\n"
more_end:
buffer:
    .space 64
word:
    .space 8
