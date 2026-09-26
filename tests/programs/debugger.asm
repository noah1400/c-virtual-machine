; The debugger stops at breakpoints and DEBUG instructions and steps over calls
; vm-args: -d

.text
main:
    LOAD R8, #2
    CALL double
    DEBUG
    CALL double
    HALT

double:
    ADD R8, R8
    RET
