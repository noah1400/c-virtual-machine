; Printing helpers for the tests; they preserve every register except R5

; Prints R8 as a signed decimal number and a newline
print_int:
    PUSH R0
    MOVE R0, R8
    SYSCALL #1
    LOAD R0, #'\n'
    SYSCALL #0
    POP R0
    RET

; Prints R8 in hexadecimal and a newline
print_hex:
    PUSH R0
    MOVE R0, R8
    SYSCALL #5
    LOAD R0, #'\n'
    SYSCALL #0
    POP R0
    RET

; Prints the Z, N, C and O flags as letters or dashes and a newline
print_flags:
    PUSHF
    PUSH R0
    PUSH R9
    LOAD R9, [SP+8]
    LOAD R0, #'Z'
    TEST R9, #0x01
    JNZ .zero
    LOAD R0, #'-'
.zero:
    SYSCALL #0
    LOAD R0, #'N'
    TEST R9, #0x02
    JNZ .negative
    LOAD R0, #'-'
.negative:
    SYSCALL #0
    LOAD R0, #'C'
    TEST R9, #0x04
    JNZ .carry
    LOAD R0, #'-'
.carry:
    SYSCALL #0
    LOAD R0, #'O'
    TEST R9, #0x08
    JNZ .overflow
    LOAD R0, #'-'
.overflow:
    SYSCALL #0
    LOAD R0, #'\n'
    SYSCALL #0
    POP R9
    POP R0
    POPF
    RET
