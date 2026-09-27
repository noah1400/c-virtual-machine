; Text helpers for the linking tests

.global print_line, length

.text
; Prints the string at R0 and a newline
print_line:
    SYSCALL #2
    LOAD R0, #'\n'
    SYSCALL #0
    RET

; Returns in R0 the length of the string at R0
length:
    MOVE R9, R0
.next:
    LOADB R10, [R9]
    CMP R10, #0
    JZ .done
    INC R9
    JMP .next
.done:
    SUB R9, R0
    MOVE R0, R9
    RET
