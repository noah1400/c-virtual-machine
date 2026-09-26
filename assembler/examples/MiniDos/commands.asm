; commands.asm - Command handlers for MiniDOS
; Contains implementation of all supported commands

; ----- Command Handlers -----

do_help_cmd:
    ; Show help text
    LOAD R0, help_text
    SYSCALL #2
    JMP parse_cmd_done
    
do_cls_cmd:
    ; Clear the screen
    SYSCALL #8
    JMP parse_cmd_done
    
do_echo_cmd:
    ; Skip past "echo" and any whitespace
    MOVE R6, R14
    CALL skip_whitespace
    
    ; Print the rest of the command line
    MOVE R0, R6
    SYSCALL #2
    
    ; Print newline
    LOAD R0, #10
    SYSCALL #0
    JMP parse_cmd_done
    
do_exit_cmd:
    ; Exit the program
    HALT
    
do_time_cmd:
    ; Print time label
    LOAD R0, time_text
    SYSCALL #2

    ; Get the time in milliseconds and print it as seconds
    SYSCALL #32
    MOVE R7, R0
    LOAD R8, #1000
    DIV R0, R8
    SYSCALL #1

    ; Print decimal point
    LOAD R0, #46
    SYSCALL #0

    ; Print milliseconds with leading zeros
    MOVE R0, R7
    MOD R0, R8
    CALL print_3_digits

    LOAD R0, seconds_text
    SYSCALL #2
    JMP parse_cmd_done
    
do_mem_cmd:
    ; Get memory information
    SYSCALL #23
    PUSH R0
    
    ; Print total memory
    LOAD R0, mem_total_msg
    SYSCALL #2
    POP R0
    SYSCALL #1
    LOAD R0, bytes_suffix
    SYSCALL #2
    
    ; Print memory layout
    LOAD R0, mem_code_msg
    SYSCALL #2
    LOAD R0, mem_data_msg
    SYSCALL #2
    LOAD R0, mem_stack_msg
    SYSCALL #2
    LOAD R0, mem_heap_msg
    SYSCALL #2
    
    JMP parse_cmd_done
    
do_ver_cmd:
    ; Show version
    LOAD R0, version_text
    SYSCALL #2
    JMP parse_cmd_done
    
do_pause_cmd:
    ; Wait for key press
    LOAD R0, pause_text
    SYSCALL #2
    
    ; Read a character
    SYSCALL #3
    
    ; Print newline
    LOAD R0, #10
    SYSCALL #0
    JMP parse_cmd_done
    
do_color_cmd:
    ; Skip past "color" and any whitespace
    MOVE R6, R14
    CALL skip_whitespace
    
    ; Check if color code provided
    LOADB R7, [R6]
    CMP R7, #0
    JZ color_reset
    
    ; Parse color number
    CALL parse_number
    
    ; Set color (R0 now has color number)
    SYSCALL #9
    JMP parse_cmd_done
    
color_reset:
    ; Reset to default color
    LOAD R0, #0xFF
    SYSCALL #9
    JMP parse_cmd_done