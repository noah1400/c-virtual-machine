; .incbin includes the bytes of a file, or LENGTH of them from OFFSET on

.text
    LOAD R0, whole
    SYSCALL #2
    LOAD R0, part
    SYSCALL #2
    LOAD R0, #'\n'
    SYSCALL #0
    LOAD R8, #part_end - part
    CALL print_int
    HALT

.include "lib.inc"

.data
whole:
    .incbin "incbin.txt"
    .byte 0
part:
    .incbin "incbin.txt", 6, 4
part_end:
    .byte 0
