; main.asm - Main entry point for MiniDOS
; Contains initialization and main command loop

.text
    ; Initialize the OS
    JMP main

; ----- Main Program -----
main:
    ; Display welcome message
    LOAD R0, welcome_msg
    SYSCALL #2
    
command_loop:
    ; Display prompt
    LOAD R0, prompt
    SYSCALL #2
    
    ; Read command line input
    LOAD R0, input_buffer
    LOAD R5, #255
    SYSCALL #4
    
    ; Parse and execute command
    LOAD R6, input_buffer
    CALL parse_command
    
    ; Loop back for next command
    JMP command_loop

.include "data.asm"    ; Include data definitions
.include "utils.asm"   ; Include utility functions
.include "parser.asm"  ; Include command parser
.include "commands.asm"; Include command handlers