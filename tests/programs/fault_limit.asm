; expect-exit: 1
; expect-stderr: Instruction limit of 1000 reached
; vm-args: -n 1000
.text
loop:
    JMP loop
