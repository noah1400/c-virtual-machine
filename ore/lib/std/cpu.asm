; The assembly behind std/cpu. MFCR and MTCR only take a register number as an immediate, so the
; control register functions try each number in turn.
    .library

.macro READ_CONTROL n
    CMP R6, #\n
    JNZ .skip\@
    MFCR R0, #\n
    RET
.skip\@:
.endm

.macro WRITE_CONTROL n
    CMP R6, #\n
    JNZ .skip\@
    MTCR #\n, R7
    RET
.skip\@:
.endm

.text
read_port:
    LOAD R6, [SP+4]
    IN R0, R6
    RET

write_port:
    LOAD R6, [SP+4]
    LOAD R7, [SP+8]
    OUT R6, R7
    RET

enable_interrupts:
    STI
    RET

disable_interrupts:
    CLI
    RET

set_handler:
    MFCR R0, IVTB
    CMP R0, #0
    JNZ .table
    LOAD R0, cpu.vectors
    MTCR IVTB, R0
.table:
    LOAD R6, [SP+4]
    AND R6, #255
    SHL R6, #2
    ADD R6, R0
    LOAD R7, [SP+8]
    STORE R7, [R6]
    RET

read_control:
    LOAD R6, [SP+4]
    READ_CONTROL 0
    READ_CONTROL 1
    READ_CONTROL 2
    READ_CONTROL 3
    READ_CONTROL 4
    READ_CONTROL 5
    READ_CONTROL 6
    READ_CONTROL 7
    READ_CONTROL 8
    LOAD R0, #0
    RET

write_control:
    LOAD R6, [SP+4]
    LOAD R7, [SP+8]
    WRITE_CONTROL 0
    WRITE_CONTROL 1
    WRITE_CONTROL 2
    WRITE_CONTROL 3
    WRITE_CONTROL 4
    WRITE_CONTROL 5
    WRITE_CONTROL 6
    WRITE_CONTROL 7
    WRITE_CONTROL 8
    RET

halt:
    HALT

; enter_user(pc, sp) saves what the caller needs back, makes its stack the one for interrupts from user
; mode and switches with POPF, which keeps the caller's frames in backtraces, unlike IRET, and keeps the
; caller's interrupt flag
enter_user:
    PUSHF
    PUSH BP
    PUSH R8
    PUSH R9
    PUSH R10
    PUSH R11
    PUSH R12
    PUSH R13
    PUSH R14
    PUSH R15
    PUSH [cpu.user_return]
    MFCR R0, KSP
    PUSH R0
    STORE SP, [cpu.user_return]
    MOVE R0, SP
    MTCR KSP, R0
    LOAD R6, [SP+52]
    LOAD R7, [SP+56]
    LOAD R0, [SP+44]
    AND R0, #0x10
    LOAD BP, #0
    LOAD R8, #0
    LOAD R9, #0
    LOAD R10, #0
    LOAD R11, #0
    LOAD R12, #0
    LOAD R13, #0
    LOAD R14, #0
    LOAD R15, #0
    LOAD R5, #0
    MOVE SP, R7
    PUSH R0
    POPF
    LOAD R0, #0
    JMP R6
.back:
    POP R6
    MTCR KSP, R6
    POP R6
    STORE R6, [cpu.user_return]
    POP R15
    POP R14
    POP R13
    POP R12
    POP R11
    POP R10
    POP R9
    POP R8
    POP BP
    POPF
    RET

; leave_user(value) returns to where enter_user left off with an IRET frame, which also ends the
; interrupt in backtraces
leave_user:
    LOAD R0, [SP+4]
    LOAD R7, [cpu.user_return]
    LOAD R6, #11
.clear:
    PUSH #0
    LOOP R6, .clear
    PUSH #0x40
    PUSH enter_user.back
    PUSH R7
    PUSH #0
    PUSH R0
    IRET

.data
    .align 4
cpu.vectors:
    .space 1024
cpu.user_return:
    .dword 0
