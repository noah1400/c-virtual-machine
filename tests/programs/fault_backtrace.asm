; A fault report ends with the calls that led to the fault
; expect-exit: 1
; expect-stderr: vm: #2 0x0000 <main> tests/programs/fault_backtrace.asm:7: CALL outer

.text
main:
    CALL outer
    HALT

outer:
    CALL divide
    RET

divide:
    LOAD R9, #0
    DIV R8, R9
    RET
