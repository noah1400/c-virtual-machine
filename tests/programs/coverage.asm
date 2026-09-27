; -c lists how often each line of code ran and marks the lines that never ran
; vm-args: -c -

.text
main:
    LOAD R8, #3
count:
    DEC R8
    JNZ count
    CMP R8, #0
    JZ done
    LOAD R8, #1
done:
    HALT

.data
unused:
    .dword 0
