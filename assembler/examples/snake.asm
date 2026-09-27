; Snake on the text display. Steer with the arrow keys or WASD, eat the stars and avoid the walls and
; your own tail; q or Escape quits. An optional argument sets the milliseconds per step (default 100).
;
;   ./vmasm assembler/examples/snake.asm -o snake.bin && ./vm snake.bin

.equ DISPLAY_BUFFER,  0x50
.equ DISPLAY_REFRESH, 0x51
.equ KEY_STATUS,      0x60
.equ KEY_DATA,        0x61

.equ KEY_ESCAPE, 27
.equ KEY_UP,     0x100
.equ KEY_DOWN,   0x101
.equ KEY_RIGHT,  0x102
.equ KEY_LEFT,   0x103

.equ COLUMNS,      80
.equ ROWS,         25
.equ FIELD_TOP,    1                        ; the row of the top wall; row 0 shows the score
.equ FIELD_WIDTH,  COLUMNS - 2
.equ FIELD_HEIGHT, ROWS - FIELD_TOP - 2
.equ FIELD_CELLS,  FIELD_WIDTH * FIELD_HEIGHT
.equ BODY_SLOTS,   2048                     ; a power of two above FIELD_CELLS

; Attributes hold the foreground color in the low nibble and the background in the high nibble
.equ EMPTY, ' ' | 0x0700
.equ WALL,  '#' | 0x0600
.equ HEAD,  '@' | 0x0A00
.equ BODY,  'o' | 0x0200
.equ FOOD,  '*' | 0x0900
.equ TEXT_COLOR,      0x0F
.equ GAME_OVER_COLOR, 0x1F

.text
main:
    CALL read_delay
    CALL draw_field

    ; The snake starts as three cells in the middle of the field, heading right
    LOAD R9, #0
    LOAD R0, #12 * COLUMNS + 20
    LOAD R5, #BODY
.start:
    MOVE R11, R9
    SHL R11, #2
    STORE R0, [R11 + body]
    CALL put
    INC R0
    INC R9
    CMP R9, #3
    JB .start
    DEC R0
    LOAD R5, #HEAD
    CALL put
    LOAD R9, #2
    STORE R9, [head_slot]

    CALL place_food
    CALL draw_score
    LOAD R8, screen
    OUT #DISPLAY_BUFFER, R8

step:
    OUT #DISPLAY_REFRESH, R8
    LOAD R0, [delay]
    SYSCALL #31
    CALL read_key
    CMP R0, #0
    JNZ quit
    CALL move
    CMP R0, #0
    JZ step

    LOAD R0, game_over_text
    LOAD R5, #12 * COLUMNS + 34
    LOAD R6, #GAME_OVER_COLOR
    CALL put_text
    OUT #DISPLAY_REFRESH, R8

    ; Keys pressed before the crash do not count; any later key or the end of input quits
.drain:
    IN R9, #KEY_STATUS
    TEST R9, #1
    JZ .wait
    IN R9, #KEY_DATA
    JMP .drain
.wait:
    IN R9, #KEY_STATUS
    CMP R9, #0
    JNZ quit
    LOAD R0, #20
    SYSCALL #31
    JMP .wait

quit:
    LOAD R8, #0
    OUT #DISPLAY_BUFFER, R8
    LOAD R0, score_text
    SYSCALL #2
    LOAD R0, [score]
    SYSCALL #1
    LOAD R0, #'\n'
    SYSCALL #0
    HALT

; Takes at most one key per step, so quick turns follow one another; returns R0 = 1 to quit
read_key:
    LOAD R0, #0
    IN R9, #KEY_STATUS
    TEST R9, #1
    JZ .done
    IN R9, #KEY_DATA
    CMP R9, #'q'
    JE .quit
    CMP R9, #KEY_ESCAPE
    JE .quit
    LOAD R10, #-COLUMNS
    CMP R9, #KEY_UP
    JE .turn
    CMP R9, #'w'
    JE .turn
    LOAD R10, #COLUMNS
    CMP R9, #KEY_DOWN
    JE .turn
    CMP R9, #'s'
    JE .turn
    LOAD R10, #1
    CMP R9, #KEY_RIGHT
    JE .turn
    CMP R9, #'d'
    JE .turn
    LOAD R10, #-1
    CMP R9, #KEY_LEFT
    JE .turn
    CMP R9, #'a'
    JNE .done
.turn:
    ; The snake cannot reverse into its own body
    LOAD R11, [direction]
    ADD R11, R10
    JZ .done
    STORE R10, [direction]
.done:
    RET
.quit:
    LOAD R0, #1
    RET

; Advances the snake by one cell; returns R0 = 1 when it crashed or filled the field
move:
    LOAD R9, [head_slot]
    MOVE R11, R9
    SHL R11, #2
    LOAD R12, [R11 + body]
    ADD R12, [direction]
    MOVE R0, R12
    CALL cell_at
    LOAD R13, #1
    CMP R5, #'*'
    JE .advance

    ; Without food the tail moves on, so the head may take the cell the tail leaves
    LOAD R13, #0
    LOAD R9, [tail_slot]
    MOVE R11, R9
    SHL R11, #2
    LOAD R10, [R11 + body]
    CMP R5, #' '
    JE .tail
    CMP R12, R10
    JNE .crash
.tail:
    MOVE R0, R10
    LOAD R5, #EMPTY
    CALL put
    INC R9
    AND R9, #BODY_SLOTS - 1
    STORE R9, [tail_slot]

.advance:
    LOAD R9, [head_slot]
    MOVE R11, R9
    SHL R11, #2
    LOAD R0, [R11 + body]
    LOAD R5, #BODY
    CALL put
    INC R9
    AND R9, #BODY_SLOTS - 1
    STORE R9, [head_slot]
    MOVE R11, R9
    SHL R11, #2
    STORE R12, [R11 + body]
    MOVE R0, R12
    LOAD R5, #HEAD
    CALL put

    LOAD R0, #0
    CMP R13, #0
    JZ .done
    LOAD R9, [score]
    INC R9
    STORE R9, [score]
    CALL draw_score
    JMP place_food
.done:
    RET
.crash:
    LOAD R0, #1
    RET

; Puts food on a random free cell of the field; returns R0 = 1 if there is none
place_food:
    LOAD R0, #FIELD_CELLS
    SYSCALL #40
    LOAD R10, #FIELD_CELLS
.try:
    MOVE R9, R0
    DIV R9, #FIELD_WIDTH
    ADD R9, #FIELD_TOP + 1
    MUL R9, #COLUMNS
    MOVE R11, R0
    MOD R11, #FIELD_WIDTH
    ADD R9, R11
    INC R9
    MOVE R11, R9
    SHL R11, #1
    LOADB R11, [R11 + screen]
    CMP R11, #' '
    JE .found
    INC R0
    CMP R0, #FIELD_CELLS
    JB .next
    LOAD R0, #0
.next:
    DEC R10
    JNZ .try
    LOAD R0, #1
    RET
.found:
    MOVE R0, R9
    LOAD R5, #FOOD
    CALL put
    LOAD R0, #0
    RET

; Clears the screen, draws the walls and the title line
draw_field:
    LOAD R0, #0
.clear:
    LOAD R5, #EMPTY
    CMP R0, #FIELD_TOP * COLUMNS
    JB .cell
    LOAD R5, #WALL
    CMP R0, #(FIELD_TOP + 1) * COLUMNS
    JB .cell
    CMP R0, #(ROWS - 1) * COLUMNS
    JAE .cell
    MOVE R9, R0
    MOD R9, #COLUMNS
    JZ .cell
    CMP R9, #COLUMNS - 1
    JE .cell
    LOAD R5, #EMPTY
.cell:
    CALL put
    INC R0
    CMP R0, #ROWS * COLUMNS
    JB .clear

    LOAD R0, title_text
    LOAD R5, #1
    LOAD R6, #TEXT_COLOR
    JMP put_text

draw_score:
    LOAD R0, [score]
    LOAD R5, #COLUMNS - 2
    LOAD R6, #TEXT_COLOR
    JMP put_number

; Returns the character of cell R0 in R5
cell_at:
    MOVE R5, R0
    SHL R5, #1
    LOADB R5, [R5 + screen]
    RET

; Stores the character and attribute in R5 into cell R0
put:
    MOVE R11, R0
    SHL R11, #1
    STOREW R5, [R11 + screen]
    RET

; Writes the string at R0 from cell R5 on in attribute R6
put_text:
    SHL R6, #8
.next:
    LOADB R7, [R0]
    CMP R7, #0
    JZ .done
    OR R7, R6
    MOVE R11, R5
    SHL R11, #1
    STOREW R7, [R11 + screen]
    INC R0
    INC R5
    JMP .next
.done:
    RET

; Writes R0 in decimal with its last digit in cell R5 and attribute R6
put_number:
    SHL R6, #8
.digit:
    MOVE R7, R0
    MOD R7, #10
    ADD R7, #'0'
    OR R7, R6
    MOVE R11, R5
    SHL R11, #1
    STOREW R7, [R11 + screen]
    DEC R5
    DIV R0, #10
    JNZ .digit
    RET

; The first program argument, if there is one, gives the milliseconds per step
read_delay:
    LOAD R0, #1
    LOAD R5, argument
    LOAD R6, #12
    SYSCALL #34
    CMP R0, #-1
    JE .done
    LOAD R6, argument
    LOAD R7, #0
.digit:
    LOADB R9, [R6]
    SUB R9, #'0'
    CMP R9, #10
    JAE .end
    MUL R7, #10
    ADD R7, R9
    INC R6
    JMP .digit
.end:
    STORE R7, [delay]
.done:
    RET

.data
delay:
    .dword 100
direction:
    .dword 1
score:
    .dword 0
head_slot:
    .dword 0
tail_slot:
    .dword 0
argument:
    .space 12
title_text:
    .asciiz "SNAKE   arrows or WASD steer, q quits                              score:"
game_over_text:
    .asciiz " GAME OVER "
score_text:
    .asciiz "Score: "
body:
    .space BODY_SLOTS * 4
screen:
    .space COLUMNS * ROWS * 2
