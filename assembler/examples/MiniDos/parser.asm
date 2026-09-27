; parser.asm - Command parsing for MiniDOS
; Contains functions to parse and execute commands

; ----- Command Parser -----
; R6 = pointer to command string
parse_command:
    PUSH R15_LR  ; Save return address
    
    ; Skip leading whitespace
    CALL skip_whitespace
    
    ; Check if empty command
    LOADB R7, [R6]
    CMP R7, #0
    JZ parse_cmd_done
    
    ; Store command start
    MOVE R8, R6
    
    ; Find end of command word
    CALL find_word_end
    
    ; Calculate command length
    MOVE R9, R6
    SUB R9, R8
    
    ; Copy command to buffer (limited to 31 chars)
    CMP R9, #31
    JBE copy_cmd_len_ok
    LOAD R9, #31
    
copy_cmd_len_ok:
    LOAD R10, command_buffer  ; Destination
    MOVE R11, R8              ; Source
    MOVE R12, R9              ; Length
    
    ; Copy loop
    CMP R12, #0
    JZ copy_cmd_done
    
copy_cmd_loop:
    LOADB R13, [R11]
    STOREB R13, [R10]
    INC R10
    INC R11
    DEC R12
    CMP R12, #0
    JNZ copy_cmd_loop
    
copy_cmd_done:    
    ; Null-terminate command
    LOAD R13, #0
    STOREB R13, [R10]
    
    ; Save pointer to arguments
    MOVE R14, R6
    
    ; Look the command up in the command table
    LOAD R10, command_table
find_cmd_loop:
    LOAD R7, [R10]
    CMP R7, #0
    JZ cmd_unknown
    LOAD R6, command_buffer
    CALL strcmp
    CMP R0, #0
    JZ cmd_found
    ADD R10, #8
    JMP find_cmd_loop

cmd_found:
    JMP [R10+4]

cmd_unknown:
    LOAD R0, cmd_not_found
    SYSCALL #2
    
parse_cmd_done:
    POP R15_LR
    RET
