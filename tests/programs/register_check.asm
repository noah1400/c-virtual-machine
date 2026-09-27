; vmasm -W warns when a call or a syscall overwrites a register value that is read afterwards
; expect-warning: register_check.asm:10: warning: R6 set at line 8 is overwritten by CALL load at line 9
; expect-warning: register_check.asm:13: warning: R9 set at line 11 is overwritten by CALL row at line 12
; expect-warning: register_check.asm:17: warning: R5 set at line 14 is overwritten by SYSCALL #0 at line 16

.text
main:
    CALL name
    CALL load
    CALL show
    LOAD R9, #2
    CALL row
    ADD R8, R9
    LOAD R5, #7
    LOAD R0, #'\n'
    SYSCALL #0
    ADD R8, R5
    HALT

; Returns the address of a text in R6
name:
    LOAD R6, text
    RET

; Uses R6 only to pass an argument
load:
    LOAD R6, #1
    CALL put
    RET

put:
    MOVE R0, R6
    SYSCALL #1
    RET

show:
    MOVE R0, R6
    SYSCALL #1
    RET

; Returns 0 in R9
row:
    LOAD R9, #0
    RET

.data
text:
    .asciiz "text"
