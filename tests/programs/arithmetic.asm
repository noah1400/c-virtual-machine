; Arithmetic results and the flags they set

.text
    LOAD R8, #7
    ADD R8, #5
    CALL print_int
    CALL print_flags

    SUB R8, #20
    CALL print_int
    CALL print_flags

    LOAD R8, #0x7FFF
    SHL R8, #16
    OR R8, #0xFFFF
    ADD R8, #1
    CALL print_hex
    CALL print_flags

    LOAD R8, #0
    NOT R8
    ADD R8, #1
    CALL print_int
    CALL print_flags

    LOAD R8, #50000
    ADD R8, R8
    MOVE R9, R8
    MUL R8, R9
    CALL print_int
    CALL print_flags

    LOAD R8, #100
    DIV R8, #7
    CALL print_int
    LOAD R8, #100
    MOD R8, #7
    CALL print_int

    LOAD R8, #5
    NEG R8
    CALL print_int
    INC R8
    CALL print_int
    DEC R8
    DEC R8
    CALL print_int

    ; 0x1_FFFFFFFF + 0x1_00000001 with a carry between the words
    LOAD R8, #0
    NOT R8
    LOAD R9, #1
    ADD R8, #1
    ADDC R9, #1
    CALL print_hex
    MOVE R8, R9
    CALL print_hex

    ; 0x3_00000000 - 1 with a borrow between the words
    LOAD R8, #0
    LOAD R9, #3
    SUB R8, #1
    SUBC R9, #0
    CALL print_hex
    MOVE R8, R9
    CALL print_hex

    LOAD R8, #3
    CMP R8, #5
    CALL print_flags
    LOAD R8, #5
    CMP R8, #3
    CALL print_flags
    CMP R8, #5
    CALL print_flags
    HALT

.include "lib.inc"
