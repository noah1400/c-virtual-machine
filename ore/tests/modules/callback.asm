; The assembly of callback.ore, which counts its runs in callback.runs and passes them to callback.called,
; which removes the argument as it returns
.text
run:
    LOAD R0, [callback.runs]
    INC R0
    STORE R0, [callback.runs]
    PUSH R0
    CALL callback.called
    RET
