; Syscall 35 stops the program with a message, reported as a fault would be, leaving out as many calls as
; R5 says so that a runtime's own error routine does not show
; expect-exit: 1

.text
main:
    CALL check
    HALT

check:
    PUSH #10
    PUSH #12
    CALL index_error
    RET

index_error:
    LOAD R0, message
    LOAD R5, #1
    SYSCALL #35
    HALT

.data
message:
    .asciiz "index 12 is out of bounds for length 10"
