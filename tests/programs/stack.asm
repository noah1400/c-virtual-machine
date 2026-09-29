; Recursion with stack frames, flag saving, PUSHA/POPA and PUSHM/POPM

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

    ; PUSHA leaves Rn at [SP + 4 * n]
    PUSHA
    LOAD R8, [SP+36]
    CALL print_int
    POPA

    ; PUSHM and POPM save and restore a range of registers the same way
    LOAD R8, #33
    LOAD R9, #44
    LOAD R10, #55
    PUSHM R8, R10
    LOAD R8, #0
    LOAD R9, #0
    LOAD R10, #0
    POPM R8, R10
    CALL print_int
    MOVE R8, R9
    CALL print_int
    MOVE R8, R10
    CALL print_int

    ; PUSHM leaves Rn at [SP + 4 * (n - first)]
    PUSHM R9, R10
    LOAD R8, [SP+4]
    CALL print_int
    POPM R9, R10

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
