; LOAD accepts any 32-bit constant and LOADHI replaces the upper half of a register

.equ BIG, 0x12345678

.text
    LOAD R8, #BIG
    CALL print_hex
    LOAD R8, #-1
    CALL print_int
    LOAD R8, #-100000
    CALL print_int
    LOAD R8, #70000
    CALL print_int

    LOAD R9, #0x5678
    LOADHI R9, #0xABCD
    MOVE R8, R9
    CALL print_hex

short:
    LOAD R8, #0xFFFF
wide:
    LOAD R8, #0x10000
done:
    LOAD R8, #wide - short
    CALL print_int
    LOAD R8, #done - wide
    CALL print_int
    HALT

.include "lib.inc"
