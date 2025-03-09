; Minimal DOS-like OS for VM
; Memory Layout:
; - Code: 0x0000-0x3FFF
; - Data: 0x4000-0x7FFF
; - Stack: 0x8000-0xBFFF
; - Heap: 0xC000-0xFFFF

; Register Usage:
; R0_ACC - Accumulator
; R1_BP - Base Pointer (reserved)
; R2_SP - Stack Pointer (reserved)
; R3_PC - Program Counter (reserved)
; R4_SR - Status Register (reserved)
; R5 - Used by syscalls, avoid
; R6-R14 - General purpose registers
; R15_LR - Link Register

.text
    ; Initialize the OS
    JMP main

; ----- Data Section -----
.data
welcome_msg:
    .asciiz "MiniDOS v1.0\nCopyright (c) 2025\nType 'help' for commands\n"
prompt:
    .asciiz "\nA:\\> "
help_text:
    .asciiz "Available commands:\n  help - Show this help\n  cls - Clear screen\n  echo [text] - Display text\n  exit - Quit OS\n  time - Show system time\n  mem - Show memory info\n  ver - Show version\n  pause - Wait for key press\n  color [num] - Change text color (0-7)\n"
cmd_not_found:
    .asciiz "Bad command or file name\n"
cmd_help:
    .asciiz "help"
cmd_cls:
    .asciiz "cls"
cmd_echo:
    .asciiz "echo"
cmd_exit:
    .asciiz "exit"
cmd_time:
    .asciiz "time"
cmd_mem:
    .asciiz "mem"
cmd_ver:
    .asciiz "ver"
cmd_pause:
    .asciiz "pause"
cmd_color:
    .asciiz "color"
version_text:
    .asciiz "MiniDOS Version 1.0\nCopyright (c) 2025\n"
time_text:
    .asciiz "Current system time: "
seconds_text:
    .asciiz " seconds\n"
pause_text:
    .asciiz "Press any key to continue..."
input_buffer:
    .space 256
command_buffer:
    .space 32
mem_total_msg:
    .asciiz "Total memory: "
mem_code_msg:
    .asciiz "Code segment: 0x0000-0x3FFF (16KB)\n"
mem_data_msg:
    .asciiz "Data segment: 0x4000-0x7FFF (16KB)\n"
mem_stack_msg:
    .asciiz "Stack segment: 0x8000-0xBFFF (16KB)\n"
mem_heap_msg:
    .asciiz "Heap segment: 0xC000-0xFFFF (16KB)\n"
bytes_suffix:
    .asciiz " bytes\n"
kb_suffix:
    .asciiz " KB\n"

; ----- Main Program -----
.text
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
    
    ; Match command against known commands
    LOAD R6, command_buffer
    
    ; Check for "help" command
    LOAD R7, cmd_help
    CALL strcmp
    CMP R0, #0
    JZ do_help_cmd
    
    ; Check for "cls" command
    LOAD R6, command_buffer
    LOAD R7, cmd_cls
    CALL strcmp
    CMP R0, #0
    JZ do_cls_cmd
    
    ; Check for "echo" command
    LOAD R6, command_buffer
    LOAD R7, cmd_echo
    CALL strcmp
    CMP R0, #0
    JZ do_echo_cmd
    
    ; Check for "exit" command
    LOAD R6, command_buffer
    LOAD R7, cmd_exit
    CALL strcmp
    CMP R0, #0
    JZ do_exit_cmd
    
    ; Check for "time" command
    LOAD R6, command_buffer
    LOAD R7, cmd_time
    CALL strcmp
    CMP R0, #0
    JZ do_time_cmd
    
    ; Check for "mem" command
    LOAD R6, command_buffer
    LOAD R7, cmd_mem
    CALL strcmp
    CMP R0, #0
    JZ do_mem_cmd
    
    ; Check for "ver" command
    LOAD R6, command_buffer
    LOAD R7, cmd_ver
    CALL strcmp
    CMP R0, #0
    JZ do_ver_cmd
    
    ; Check for "pause" command
    LOAD R6, command_buffer
    LOAD R7, cmd_pause
    CALL strcmp
    CMP R0, #0
    JZ do_pause_cmd
    
    ; Check for "color" command
    LOAD R6, command_buffer
    LOAD R7, cmd_color
    CALL strcmp
    CMP R0, #0
    JZ do_color_cmd
    
    ; Command not found
    LOAD R0, cmd_not_found
    SYSCALL #2
    JMP parse_cmd_done
    
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
    
    ; Get and display system time
    SYSCALL #32
    PUSH R0
    
    ; Convert to seconds
    PUSH R7
    LOAD R7, #1000
    DIV R0, R7
    
    ; Print seconds
    SYSCALL #1
    
    ; Print decimal point
    LOAD R0, #46
    SYSCALL #0
    
    ; Print milliseconds
    POP R0
    MOD R0, R7

    POP R7
    
    ; Ensure we print leading zeros
    CMP R0, #100
    JP print_msec
    ; Less than 100, need to print at least one zero
    LOAD R0, #48
    SYSCALL #0
    
    CMP R0, #10
    JP print_msec
    ; Less than 10, need two leading zeros
    LOAD R0, #48
    SYSCALL #0
    
print_msec:
    SYSCALL #1
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
    
parse_cmd_done:
    POP R15_LR
    RET

; ----- Helper Functions -----

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