; The profile sums executed instructions under the closest preceding label
; vm-args: -p
; expect-stderr:           30   48.4%  0x0014 <square>

.text
main:
    LOAD R8, #10
.loop:
    CALL square
    DEC R8
    JNZ .loop
    HALT

square:
    MOVE R9, R8
    MUL R9, R9
    RET
