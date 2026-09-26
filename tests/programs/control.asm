; Conditional jumps, calls, loops and indirect jumps

.text
    ; For each pair print which of JZ JNZ JN JP JO JC JBE JA branch after CMP
    LOAD R8, #3
    LOAD R9, #5
    CALL show_branches
    LOAD R8, #5
    LOAD R9, #3
    CALL show_branches
    LOAD R8, #5
    LOAD R9, #5
    CALL show_branches

    LOAD R10, #4
    LOAD R8, #0
count:
    INC R8
    LOOP R10, count
    CALL print_int

    ; Indirect calls through a register and through memory
    LOAD R11, handlers
    LOAD R12, [R11+4]
    CALL R12
    CALL [R11+8]
    JMP [R11]

finish:
    HALT

show_branches:
    CMP R8, R9
    LOAD R0, #'z'
    JZ .z
    LOAD R0, #'-'
.z: SYSCALL #0
    LOAD R0, #'n'
    JNZ .nz
    LOAD R0, #'-'
.nz: SYSCALL #0
    LOAD R0, #'N'
    JN .n
    LOAD R0, #'-'
.n: SYSCALL #0
    LOAD R0, #'P'
    JP .p
    LOAD R0, #'-'
.p: SYSCALL #0
    LOAD R0, #'O'
    JO .o
    LOAD R0, #'-'
.o: SYSCALL #0
    LOAD R0, #'C'
    JC .c
    LOAD R0, #'-'
.c: SYSCALL #0
    LOAD R0, #'b'
    JBE .be
    LOAD R0, #'-'
.be: SYSCALL #0
    LOAD R0, #'a'
    JA .a
    LOAD R0, #'-'
.a: SYSCALL #0
    LOAD R0, #'\n'
    SYSCALL #0
    RET

say_one:
    LOAD R0, one_text
    SYSCALL #2
    RET

say_two:
    LOAD R0, two_text
    SYSCALL #2
    RET

say_done:
    LOAD R0, done_text
    SYSCALL #2
    JMP finish

.include "lib.inc"

.data
handlers:
    .dword say_done, say_one, say_two
one_text:
    .asciiz "called through a register\n"
two_text:
    .asciiz "called through memory\n"
done_text:
    .asciiz "jumped through memory\n"
