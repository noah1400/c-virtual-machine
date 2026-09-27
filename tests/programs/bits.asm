; Multiply high, set on condition, bit counts and byte swap

.text
    LOAD R8, #0x10000
    MULH R8, #0x10000
    CALL print_int
    LOAD R8, #-2
    MULH R8, #3
    CALL print_int
    LOAD R8, #-2
    UMULH R8, #3
    CALL print_int

    ; SETcc takes the conditions of the jumps, including their other names
    LOAD R9, #5
    CMP R9, #7
    SETL R10
    SETG R11
    SETNE R12
    LOAD R9, #-1
    CMP R9, #1
    SETB R13
    SETL R14
    MOVE R8, R10
    CALL print_int
    MOVE R8, R11
    CALL print_int
    MOVE R8, R12
    CALL print_int
    MOVE R8, R13
    CALL print_int
    MOVE R8, R14
    CALL print_int

    LOAD R9, #0xF0F0
    POPCNT R8, R9
    CALL print_int
    CLZ R8, R9
    CALL print_int
    CTZ R8, R9
    CALL print_int
    LOAD R9, #0
    CTZ R8, R9
    CALL print_int

    LOAD R8, #0x11223344
    BSWAP R8
    CALL print_hex
    HALT

.include "lib.inc"
