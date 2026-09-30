; Every conditional jump decides after a compare, a sum, TEST, and a MUL that keeps the carry of a compare as the
; flags these leave would: one digit for each of JZ JNZ JN JP JO JC JBE JA JL JGE JLE JG JAE, 1 where it jumps

.macro TAKEN jump
    \jump .yes\@
    LOAD R0, #'0'
    JMP .shown\@
.yes\@:
    LOAD R0, #'1'
.shown\@:
    SYSCALL #0
.endm

.macro COMPARED jump
    CMP R8, R9
    TAKEN \jump
.endm

.macro SUMMED jump
    MOVE R10, R8
    ADD R10, R9
    TAKEN \jump
.endm

.macro TESTED jump
    TEST R8, R9
    TAKEN \jump
.endm

.macro MULTIPLIED jump
    CMP R8, R9
    MOVE R10, R8
    MUL R10, R9
    TAKEN \jump
.endm

.macro ALL form
    \form JZ
    \form JNZ
    \form JN
    \form JP
    \form JO
    \form JC
    \form JBE
    \form JA
    \form JL
    \form JGE
    \form JLE
    \form JG
    \form JAE
    LOAD R0, #' '
    SYSCALL #0
.endm

.macro PAIR a, b
    LOAD R8, #\a
    LOAD R9, #\b
    CALL show
.endm

.text
    PAIR 0, 0
    PAIR 5, 5
    PAIR 1, 2
    PAIR 2, 1
    PAIR 0xFFFFFFFF, 1
    PAIR 1, 0xFFFFFFFF
    PAIR 0x80000000, 1
    PAIR 0x7FFFFFFF, 0xFFFFFFFF
    PAIR 0x80000000, 0x80000000
    HALT

show:
    ALL COMPARED
    ALL SUMMED
    ALL TESTED
    ALL MULTIPLIED
    LOAD R0, #'\n'
    SYSCALL #0
    RET
