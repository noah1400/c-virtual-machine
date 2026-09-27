; -L prints registers and memory each time execution reaches a location, without stopping it
; vm-args: -L count:R8:d,[total]:d,[name]:s,[name+1]:bc -L done:R8

.text
main:
    LOAD R8, #3
count:
    LOAD R9, [total]
    ADD R9, R8
    STORE R9, [total]
    DEC R8
    JNZ count
done:
    HALT

.data
total:
    .dword 0
name:
    .asciiz "sum"
