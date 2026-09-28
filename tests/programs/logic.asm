; Bitwise operations, shifts and rotations

.text
    LOAD R8, #0xF0F0
    AND R8, #0xFF00
    CALL print_hex
    LOAD R8, #0xF0F0
    OR R8, #0xFF00
    CALL print_hex
    LOAD R8, #0xF0F0
    XOR R8, #0xFF00
    CALL print_hex
    LOAD R8, #0
    NOT R8
    CALL print_hex
    CALL print_flags

    LOAD R8, #1
    SHL R8, #31
    CALL print_hex
    SHL R8, #1
    CALL print_hex
    CALL print_flags

    LOAD R8, #1
    SHL R8, #31
    SHR R8, #31
    CALL print_hex
    LOAD R8, #1
    SHL R8, #31
    SAR R8, #4
    CALL print_hex

    LOAD R8, #1
    SHL R8, #31
    OR R8, #1
    ROL R8, #1
    CALL print_hex
    CALL print_flags
    ROR R8, #1
    CALL print_hex
    CALL print_flags

    ; Rotating by zero leaves the carry alone
    LOAD R8, #1
    CMP R8, #0
    ROL R8, #0
    CALL print_flags
    LOAD R8, #1
    SHL R8, #31
    ROR R8, #0
    CALL print_flags

    LOAD R8, #0x10
    TEST R8, #1
    CALL print_flags
    CALL print_hex

    LOAD R8, #5
    LOAD R9, #3
    SHL R8, R9
    CALL print_int
    HALT

.include "lib.inc"
