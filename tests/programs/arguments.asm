; Reads the program arguments with syscall 34, including a truncated copy and a length query
; program-args: alpha beta

.text
    LOAD R8, #0
    LOAD R9, #-1
.next:
    MOVE R0, R8
    LOAD R5, buffer
    LOAD R6, #64
    SYSCALL #34
    CMP R0, R9
    JZ .done
    LOAD R0, buffer
    SYSCALL #2
    LOAD R0, #'\n'
    SYSCALL #0
    INC R8
    JMP .next

.done:
    ; A four byte buffer receives three characters and the terminator
    LOAD R0, #1
    LOAD R5, buffer
    LOAD R6, #4
    SYSCALL #34
    MOVE R8, R0
    CALL print_int
    LOAD R0, buffer
    SYSCALL #2
    LOAD R0, #'\n'
    SYSCALL #0

    ; A zero size only reports the length
    LOAD R0, #2
    LOAD R6, #0
    SYSCALL #34
    MOVE R8, R0
    CALL print_int
    HALT

.include "lib.inc"

.data
buffer:
    .space 64
