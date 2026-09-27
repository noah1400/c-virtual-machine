; Operands that do not fit the first word are stored in an extension word

.equ BIG, 0x12345678

.text
    LOAD R8, #BIG
    CALL print_hex
    LOAD R8, #-1
    CALL print_int
    LOAD R8, #-100000
    CALL print_int

    LOAD R8, #70000
    ADD R8, #100000
    CALL print_int
    AND R8, #0x1FFFF
    CALL print_hex

    LOAD R9, #table - 3000
    LOAD R8, [R9 + 3004]
    CALL print_int

    ; Short immediates are sign-extended
    LOAD R8, #10
    ADD R8, #-15
    CALL print_int

short:
    LOAD R8, #-0x8000
wide:
    LOAD R8, #0x8000
done:
    LOAD R8, #wide - short
    CALL print_int
    LOAD R8, #done - wide
    CALL print_int
    HALT

.include "lib.inc"

.data
table:
    .dword 11, 22
