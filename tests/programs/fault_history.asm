; With -H, the report of a failed run starts with the last instructions and the registers they changed
; vm-args: -H 3
; expect-exit: 1
; expect-stderr: 0x0008 <main+8>             ADD R8, R9               R8=0x00000005

.text
main:
    LOAD R8, #2
    LOAD R9, #3
    ADD R8, R9
    LOAD R10, #0
    DIV R8, R10
