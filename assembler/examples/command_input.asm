; Example command processor in VM assembly
.text
    ; Initialize
    LOAD R0, welcome_msg
    SYSCALL #2            ; Print welcome message
    
command_loop:
    ; Display prompt
    LOAD R0, prompt_msg
    SYSCALL #2
    
    ; Read command string (up to 100 chars)
    LOAD R0, input_buffer
    LOAD R5, #100
    SYSCALL #4            ; Read string

    LOAD R11, input_buffer
    
    ; Parse command (simplified example)
    LOADB R1, [input_buffer]   ; First character
    
    ; Check for 'q' quit command
    LOAD R0, #113         ; ASCII 'q'
    CMP R1, R0
    JZ exit_program
    
    ; Check for 'h' help command
    LOAD R0, #104         ; ASCII 'h'
    CMP R1, R0
    JZ show_help
    
    ; Unknown command
    LOAD R0, unknown_cmd
    DEBUG
    SYSCALL #2
    JMP command_loop
    
show_help:
    LOAD R0, help_text
    SYSCALL #2
    JMP command_loop
    
exit_program:
    LOAD R0, goodbye_msg
    SYSCALL #2
    HALT

.data
welcome_msg:
    .asciiz "Welcome to Command Processor\n"
prompt_msg:
    .asciiz "\n> "
help_text:
    .asciiz "Available commands:\n  h - Show help\n  q - Quit\n"
unknown_cmd:
    .asciiz "Unknown command. Type 'h' for help.\n"
goodbye_msg:
    .asciiz "Goodbye!\n"
input_buffer:
    .space 100  ; First 8 bytes of buffer