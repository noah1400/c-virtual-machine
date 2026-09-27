; The display draws its 80x25 buffer of characters and colors, and later refreshes draw only what changed

.equ DISPLAY_BUFFER,  0x50
.equ DISPLAY_REFRESH, 0x51
.equ DISPLAY_COLUMNS, 0x52
.equ DISPLAY_ROWS,    0x53

.text
    LOAD R8, #'H' | 0x4F00
    STOREW R8, [screen]
    LOAD R8, #'i' | 0x4F00
    STOREW R8, [screen + 2]
    LOAD R8, #'!' | 0x0C00
    STOREW R8, [screen + (1 * 80 + 5) * 2]
    LOAD R8, screen
    OUT #DISPLAY_BUFFER, R8
    OUT #DISPLAY_REFRESH, R8

    LOAD R8, #'o' | 0x4F00
    STOREW R8, [screen + 2]
    OUT #DISPLAY_REFRESH, R8
    OUT #DISPLAY_REFRESH, R8

    LOAD R8, #0
    OUT #DISPLAY_BUFFER, R8
    IN R8, #DISPLAY_COLUMNS
    CALL print_int
    IN R8, #DISPLAY_ROWS
    CALL print_int
    HALT

.include "lib.inc"

.data
screen:
    .space 80 * 25 * 2
