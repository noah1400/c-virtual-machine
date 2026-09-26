; Prints the files named on the command line, or standard input when there are none
; Usage: vm cat.bin [file...]

.equ STDOUT, 1
.equ STDERR, 2
.equ PATH_SIZE, 256
.equ BUFFER_SIZE, 512

.text
main:
    LOAD R9, #-1                ; returned for a missing argument
    LOAD R0, #1
    LOAD R6, #0
    SYSCALL #34                 ; length of the first argument
    CMP R0, R9
    JNZ .files
    LOAD R0, #0                 ; no files: copy standard input
    CALL copy
    HALT

.files:
    LOAD R8, #1
.next:
    MOVE R0, R8
    LOAD R5, path
    LOAD R6, #PATH_SIZE
    SYSCALL #34
    CMP R0, R9
    JZ .done
    LOAD R0, path
    LOAD R5, #0                 ; open for reading
    SYSCALL #10
    CMP R5, #0
    JNZ .missing
    MOVE R10, R0
    CALL copy
    MOVE R0, R10
    SYSCALL #11
    INC R8
    JMP .next
.done:
    HALT

.missing:
    LOAD R0, #STDERR
    LOAD R5, cannot_open
    CALL write_string
    LOAD R0, #STDERR
    LOAD R5, path
    CALL write_string
    LOAD R0, #STDERR
    LOAD R5, newline
    CALL write_string
    LOAD R0, #1
    SYSCALL #30

; Copies everything from the file handle in R0 to standard output
copy:
    MOVE R11, R0
.read:
    MOVE R0, R11
    LOAD R5, buffer
    LOAD R6, #BUFFER_SIZE
    SYSCALL #12
    CMP R0, #0
    JZ .end
    MOVE R6, R0
    LOAD R0, #STDOUT
    LOAD R5, buffer
    SYSCALL #13
    JMP .read
.end:
    RET

; Writes the NUL-terminated string at R5 to the file handle in R0
write_string:
    MOVE R6, R5
.length:
    LOADB R7, [R6]
    CMP R7, #0
    JZ .write
    INC R6
    JMP .length
.write:
    SUB R6, R5
    SYSCALL #13
    RET

.data
cannot_open:
    .asciiz "cat: cannot open "
newline:
    .asciiz "\n"
path:
    .space PATH_SIZE
buffer:
    .space BUFFER_SIZE
