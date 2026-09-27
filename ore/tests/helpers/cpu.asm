; Privileged instructions for the Ore tests that need them, called through extern fn
.text
; set_vector(vector: int, handler: u32) puts a handler into the interrupt vector table
set_vector:
    MFCR R0, IVTB
    CMP R0, #0
    JNZ .table
    LOAD R0, vectors
    MTCR IVTB, R0
.table:
    LOAD R6, [SP+4]
    SHL R6, #2
    ADD R6, R0
    LOAD R7, [SP+8]
    STORE R7, [R6]
    RET

raise_test_interrupt:
    INT #0x40
    RET

.data
    .align 4
vectors:
    .space 1024
