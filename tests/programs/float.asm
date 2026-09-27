; Single-precision floating point in the general registers

.text
    LOAD R8, #7.5
    FADD R8, #2.25
    CALL print_float            ; 9.75
    FMUL R8, #-2.0
    CALL print_float            ; -19.5
    FABS R8
    FDIV R8, #4.0
    CALL print_float            ; 4.875
    LOAD R8, #2.0
    FSQRT R8, R8
    CALL print_float            ; 1.41421
    FNEG R8
    CALL print_float            ; -1.41421

    ; Conversions truncate toward zero; NaN or overflow give INT32_MIN and set O
    LOAD R9, #-3.75
    FTOI R8, R9
    CALL print_int              ; -3
    LOAD R9, #3e10
    FTOI R8, R9
    PUSHF
    CALL print_int              ; -2147483648
    POPF
    CALL print_flags            ; ---O plus N
    LOAD R9, #-7
    ITOF R8, R9
    CALL print_float            ; -7

    ; Compare with the unsigned conditions; NaN compares unordered
    LOAD R9, #1.5
    FCMP R9, #2.5
    JB .less
    LOAD R0, wrong_text
    SYSCALL #2
.less:
    FCMP R9, #1.5
    JZ .equal
    LOAD R0, wrong_text
    SYSCALL #2
.equal:
    LOAD R9, #0.0
    FDIV R9, #0.0
    FCMP R9, #1.0
    JO .unordered
    LOAD R0, wrong_text
    SYSCALL #2
.unordered:
    LOAD R8, #1.0
    FDIV R8, #0.0
    CALL print_float            ; inf

    LOAD R8, [values + 4]
    CALL print_float            ; 42
    HALT

print_float:
    MOVE R0, R8
    SYSCALL #7
    LOAD R0, #'\n'
    SYSCALL #0
    RET

.include "lib.inc"

.data
values:
    .float 0.5, 42, -1e-3
wrong_text:
    .asciiz "wrong\n"
