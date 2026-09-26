; Every addressing mode for loads, stores and LEA

.text
    LOAD R8, [table]
    CALL print_int

    LOAD R9, table
    LOAD R8, [R9]
    CALL print_int
    LOAD R8, [R9+8]
    CALL print_int
    LEA R10, [R9+12]
    LOAD R8, [R10-4]
    CALL print_int

    LOADB R8, [bytes+2]
    CALL print_int
    LOADW R8, [words+2]
    CALL print_hex
    LOAD R8, #0x1FF
    LOADB R8, R8
    CALL print_hex

    ; Partial stores only touch their own bytes
    LOAD R11, #0x7F
    STOREB R11, [R9]
    LOAD R11, #0xABCD
    STOREW R11, [R9+4]
    LOAD R8, [R9]
    CALL print_int
    LOAD R8, [R9+4]
    CALL print_hex

    ; Locals below BP, reached through BP and SP
    ENTER #8
    LOAD R11, #1234
    STORE R11, [BP-4]
    LOAD R8, [SP+4]
    CALL print_int
    LEAVE

    ; Arguments above the saved BP and return address
    PUSH #42
    CALL read_argument
    MOVE R8, R0
    CALL print_int
    HALT

read_argument:
    ENTER #0
    LOAD R0, [BP+8]
    LEAVE
    RET #4

.include "lib.inc"

.data
table:
    .dword 10, 20, 30, 40
bytes:
    .byte 1, 2, 0xFF
words:
    .word 0x1234, 0xBEEF
