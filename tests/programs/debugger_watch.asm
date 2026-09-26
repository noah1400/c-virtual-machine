; The debugger stops after an instruction changes a watched word
; vm-args: -d

.text
main:
    LOAD R8, #0
.loop:
    INC R8
    STORE R8, [counter]
    CMP R8, #3
    JNZ .loop
    HALT

.data
counter:
    .dword 0
