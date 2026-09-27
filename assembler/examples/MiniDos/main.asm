; main.asm - Main entry point for MiniDOS
; Contains initialization and main command loop

.entry main

.text

; ----- Main Program -----
main:
    ; Display welcome message
    LOAD R0, welcome_msg
    SYSCALL #2
    
command_loop:
    ; Display prompt
    LOAD R0, prompt
    SYSCALL #2
    
    ; Read command line input, stopping at the end of input
    LOAD R0, input_buffer
    LOAD R5, #255
    SYSCALL #4
    CMP R5, #0
    JNZ end_of_input
    
    ; Parse and execute command
    LOAD R6, input_buffer
    CALL parse_command
    
    ; Loop back for next command
    JMP command_loop

end_of_input:
    HALT

.include "data.asm"    ; Include data definitions
.include "utils.asm"   ; Include utility functions
.include "parser.asm"  ; Include command parser
.include "commands.asm"; Include command handlers
.include "disk.asm"    ; Include the file system commands
