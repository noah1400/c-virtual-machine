; Tracing prints each executed instruction with its symbolic address on stderr
; vm-args: -t
; expect-stderr: 0x0008 <main.again+4>

.text
main:
    LOAD R8, #2
.again:
    DEC R8
    JNZ .again
    HALT
