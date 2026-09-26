; The debugger steps over recursive calls whose callee removes the arguments
; vm-args: -d

.text
main:
    PUSH #3
    CALL sum
    MOVE R8, R0
    HALT

; sum(n) = n + sum(n - 1), with n on the stack and removed by RET #4
sum:
    ENTER #0
    LOAD R0, [BP+8]
    CMP R0, #0
    JZ .done
    SUB R0, #1
    PUSH R0
    CALL sum
    ADD R0, [BP+8]
.done:
    LEAVE
    RET #4
