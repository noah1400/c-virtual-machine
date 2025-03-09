; utils.asm - Utility functions for MiniDOS
; Contains general-purpose functions for string handling, etc.

; ----- String Utilities -----

; Skip whitespace
; Input: R6 = pointer to string
; Output: R6 = pointer to first non-whitespace character
skip_whitespace:
    PUSH R7
skip_ws_loop:
    LOADB R7, [R6]
    CMP R7, #32  ; Space
    JNZ skip_ws_done
    INC R6
    JMP skip_ws_loop
skip_ws_done:
    POP R7
    RET

; Find the end of a word
; Input: R6 = pointer to start of word
; Output: R6 = pointer to character after word
find_word_end:
    PUSH R7
find_word_loop:
    LOADB R7, [R6]
    CMP R7, #0   ; End of string
    JZ find_word_done
    CMP R7, #32  ; Space
    JZ find_word_done
    CMP R7, #9   ; Tab
    JZ find_word_done
    INC R6
    JMP find_word_loop
find_word_done:
    POP R7
    RET

; String comparison
; Input: R6 = pointer to string 1
;        R7 = pointer to string 2
; Output: R0 = 0 if equal, non-zero if different
strcmp:
    PUSH R6
    PUSH R7
    PUSH R8
    PUSH R9
strcmp_loop:
    LOADB R8, [R6]
    LOADB R9, [R7]
    
    ; Check for end of strings
    CMP R8, #0
    JNZ strcmp_continue
    CMP R9, #0
    JNZ strcmp_not_equal
    ; Both at null terminator, strings are equal
    LOAD R0, #0
    JMP strcmp_done
    
strcmp_continue:
    ; Compare characters
    CMP R8, R9
    JNZ strcmp_not_equal
    
    ; Move to next character
    INC R6
    INC R7
    JMP strcmp_loop
    
strcmp_not_equal:
    ; Calculate difference (for sorting order)
    SUB R8, R9
    MOVE R0, R8
    
strcmp_done:
    POP R9
    POP R8
    POP R7
    POP R6
    RET

; ----- Number Utilities -----

; Parse a number from string
; Input: R6 = pointer to string
; Output: R0 = parsed number, R6 updated to after number
parse_number:
    PUSH R7
    PUSH R8
    LOAD R0, #0    ; Initialize result
parse_num_loop:
    LOADB R7, [R6]
    
    ; Check if character is a digit
    CMP R7, #48    ; '0'
    JN parse_num_done
    CMP R7, #57    ; '9'
    JP parse_num_done
    
    ; Multiply result by 10
    LOAD R8, #10
    MUL R0, R8
    
    ; Add new digit
    SUB R7, #48    ; Convert ASCII to number
    ADD R0, R7
    
    ; Move to next character
    INC R6
    JMP parse_num_loop
    
parse_num_done:
    POP R8
    POP R7
    RET