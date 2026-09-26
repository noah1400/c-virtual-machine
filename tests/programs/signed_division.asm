; Signed division truncates toward zero, unsigned division does not see signs

.text
    LOAD R10, #7
    NEG R10
    LOAD R11, #2
    NEG R11

    MOVE R8, R10
    IDIV R8, #2
    CALL print_int
    MOVE R8, R10
    IMOD R8, #2
    CALL print_int
    LOAD R8, #7
    IDIV R8, R11
    CALL print_int
    LOAD R8, #7
    IMOD R8, R11
    CALL print_int
    MOVE R8, R10
    IDIV R8, R11
    CALL print_int
    MOVE R8, R10
    DIV R8, #2
    CALL print_int
    HALT

.include "lib.inc"
