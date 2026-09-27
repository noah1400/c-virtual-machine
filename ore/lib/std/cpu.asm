; The assembly behind std/cpu. MFCR and MTCR only take a register number as an immediate, so the
; control register functions try each number in turn.
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

.data
    .align 4
cpu.vectors:
    .space 1024
