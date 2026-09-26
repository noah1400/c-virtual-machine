; Recursion with stack frames, flag saving and PUSHA/POPA

.text
    PUSH #6
    CALL factorial
    MOVE R8, R0
    CALL print_int

    ; POPF brings back flags that later instructions changed
    LOAD R8, #0
    CMP R8, #0
    PUSHF
    CMP R8, #1
    POPF
    CALL print_flags

    ; POPA restores registers but keeps SP and PC
    LOAD R8, #11
    LOAD R9, #22
    PUSHA
    LOAD R8, #0
    LOAD R9, #0
    POPA
    CALL print_int
    MOVE R8, R9
    CALL print_int
    MOVE R8, SP
    CALL print_hex
    HALT

; factorial(n) with n on the stack, result in R0
factorial:
    ENTER #0
    LOAD R0, [BP+8]
    CMP R0, #1
    JBE .done
    SUB R0, #1
    PUSH R0
    CALL factorial
    LOAD R9, [BP+8]
    MUL R0, R9
.done:
    LEAVE
    RET #4

.include "lib.inc"
