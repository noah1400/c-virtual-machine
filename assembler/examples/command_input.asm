; Fixed Command Processor in VM assembly with working calculator
.text
    ; Initialize
    LOAD R0, welcome_msg
    SYSCALL #2            ; Print welcome message
    
command_loop:
    ; Display prompt
    LOAD R0, prompt_msg
    SYSCALL #2
    
    ; Read command string (up to 100 chars), stopping at the end of input
    LOAD R0, input_buffer
    LOAD R5, #100
    SYSCALL #4            ; Read string
    CMP R5, #0
    JNZ end_of_input

    ; Parse and execute command
    LOAD R6, input_buffer
    CALL parse_command
    
    JMP command_loop

end_of_input:
    HALT
    
; Parse and execute the command in input_buffer
; R6 = address of input buffer
parse_command:
    ; Check for empty command
    LOADB R11, [R6]
    CMP R11, #0
    JZ parse_cmd_done
    
    ; Check for help command
    LOAD R5, help_cmd
    MOVE R7, R6
    CALL cmd_match
    CMP R0, #1
    JZ do_help_cmd
    
    ; Check for exit command
    LOAD R5, exit_cmd
    MOVE R7, R6
    CALL cmd_match
    CMP R0, #1
    JZ do_exit_cmd
    
    ; Check for echo command
    LOAD R5, echo_cmd
    MOVE R7, R6
    CALL cmd_match
    CMP R0, #1
    JZ do_echo_cmd
    
    ; Check for clear command
    LOAD R5, clear_cmd
    MOVE R7, R6
    CALL cmd_match
    CMP R0, #1
    JZ do_clear_cmd
    
    ; Check for time command
    LOAD R5, time_cmd
    MOVE R7, R6
    CALL cmd_match
    CMP R0, #1
    JZ do_time_cmd
    
    ; Check for calc command
    LOAD R5, calc_cmd
    MOVE R7, R6
    CALL cmd_match
    CMP R0, #1
    JZ do_calc_cmd
    
    ; Check for mem command
    LOAD R5, mem_cmd
    MOVE R7, R6
    CALL cmd_match
    CMP R0, #1
    JZ do_mem_cmd
    
    ; Unknown command
    LOAD R0, unknown_cmd
    SYSCALL #2
    JMP parse_cmd_done
    
do_help_cmd:
    LOAD R0, help_text
    SYSCALL #2
    JMP parse_cmd_done
    
do_exit_cmd:
    LOAD R0, goodbye_msg
    SYSCALL #2
    HALT
    
do_echo_cmd:
    ; Find the arguments part (skip "echo" and any spaces)
    MOVE R8, R6           ; Start with the command
    
    ; Skip the "echo" part
    LOAD R9, #4           ; Length of "echo"
    ADD R8, R9
    
    ; Skip any spaces
skip_spaces:
    LOADB R11, [R8]       ; Load character
    CMP R11, #32          ; Check if it's a space
    JNZ echo_output       ; If not a space, start output
    INC R8                ; Move to next character
    JMP skip_spaces       ; Continue skipping spaces
    
echo_output:
    ; Output the argument part
    MOVE R0, R8
    SYSCALL #2
    LOAD R0, #10          ; Newline
    SYSCALL #0
    JMP parse_cmd_done

do_clear_cmd:
    ; Clear the screen using syscall 8
    SYSCALL #8
    JMP parse_cmd_done
    
do_time_cmd:
    ; Display system time (syscall 32 gets time in ms)
    LOAD R0, time_msg
    SYSCALL #2
    
    SYSCALL #32           ; Get system time in ms (result in R0)
    MOVE R13, R0          ; Save time value
    
    ; Print whole seconds
    LOAD R12, #1000
    DIV R0, R12
    SYSCALL #1
    
    LOAD R0, #46          ; Period character
    SYSCALL #0
    
    ; Print milliseconds padded to three digits
    MOVE R14, R13
    MOD R14, R12
    CMP R14, #99
    JA print_ms_tens
    LOAD R0, #48          ; ASCII '0'
    SYSCALL #0
print_ms_tens:
    CMP R14, #9
    JA print_ms
    LOAD R0, #48
    SYSCALL #0
print_ms:
    MOVE R0, R14
    SYSCALL #1            ; Print milliseconds
    
    LOAD R0, seconds_msg
    SYSCALL #2
    JMP parse_cmd_done

do_calc_cmd:
    ; This is a completely rewritten calculator function
    ; Find the expression to calculate (skip "calc" and any spaces)
    MOVE R8, R6           ; Start with the command buffer

    ; Skip the "calc" part (4 characters)
    ADD R8, #4

    ; Skip any spaces
    CALL skip_whitespace  ; R8 now points to first non-space character

    ; Check if there's an expression
    LOADB R11, [R8]
    CMP R11, #0           ; Check for end of string
    JZ calc_no_expression

    ; --- Parse first operand ---
    CALL parse_number     ; Result in R0, R8 updated to point after number
    MOVE R9, R0           ; R9 = first operand
    
    ; Skip any spaces after first operand
    CALL skip_whitespace

    ; Check for end of string (single number, no operation)
    LOADB R11, [R8]
    CMP R11, #0
    JZ calc_print_number

    ; --- Parse operator ---
    LOADB R11, [R8]       ; Load operator character
    LOAD R10, #0          ; 0=add, 1=sub, 2=mul, 3=div

    CMP R11, #43          ; ASCII '+'
    JNZ not_add
    LOAD R10, #0          ; Addition
    JMP op_parsed
not_add:
    CMP R11, #45          ; ASCII '-'
    JNZ not_sub
    LOAD R10, #1          ; Subtraction
    JMP op_parsed
not_sub:
    CMP R11, #42          ; ASCII '*'
    JNZ not_mul
    LOAD R10, #2          ; Multiplication
    JMP op_parsed
not_mul:
    CMP R11, #47          ; ASCII '/'
    JNZ invalid_operator
    LOAD R10, #3          ; Division
op_parsed:
    INC R8                ; Move past operator

    ; Skip spaces after operator
    CALL skip_whitespace

    ; Check for premature end of string
    LOADB R11, [R8]
    CMP R11, #0
    JZ calc_invalid

    ; --- Parse second operand ---
    CALL parse_number     ; Result in R0, R8 updated
    MOVE R11, R0          ; R11 = second operand

    ; --- Perform calculation based on operator ---
    CMP R10, #0           ; Add?
    JNZ not_operation_add
    ADD R9, R11
    JMP calc_print_result
not_operation_add:
    CMP R10, #1           ; Subtract?
    JNZ not_operation_sub
    SUB R9, R11
    JMP calc_print_result
not_operation_sub:
    CMP R10, #2           ; Multiply?
    JNZ not_operation_mul
    MUL R9, R11
    JMP calc_print_result
not_operation_mul:
    ; Must be division
    CMP R11, #0
    JZ calc_div_by_zero
    DIV R9, R11

calc_print_result:
    ; Print result message
    LOAD R0, calc_result_msg
    SYSCALL #2
    
    ; Print calculation result
    MOVE R0, R9
    SYSCALL #1
    
    ; Print newline
    LOAD R0, #10
    SYSCALL #0
    JMP parse_cmd_done

calc_print_number:
    ; Just print the single number
    LOAD R0, calc_result_msg
    SYSCALL #2
    
    MOVE R0, R9
    SYSCALL #1
    
    LOAD R0, #10
    SYSCALL #0
    JMP parse_cmd_done

calc_no_expression:
    LOAD R0, calc_usage_msg
    SYSCALL #2
    JMP parse_cmd_done

invalid_operator:
calc_invalid:
    LOAD R0, calc_invalid_msg
    SYSCALL #2
    JMP parse_cmd_done

calc_div_by_zero:
    LOAD R0, calc_div_zero_msg
    SYSCALL #2
    JMP parse_cmd_done

; Skip whitespace characters
; Input: R8 = pointer to string
; Output: R8 = pointer to first non-whitespace character
skip_whitespace:
    LOADB R11, [R8]
    CMP R11, #32          ; Space?
    JNZ skip_whitespace_done
    INC R8
    JMP skip_whitespace
skip_whitespace_done:
    RET

; Parse a number from string
; Input: R8 = pointer to start of number
; Output: R0 = parsed number, R8 = pointer to character after number
parse_number:
    LOAD R0, #0           ; Initialize result

parse_number_loop:
    LOADB R11, [R8]       ; Read a character
    
    ; Check if character is a digit
    CMP R11, #48          ; ASCII '0'
    JN parse_number_done  ; Not a digit - we're done
    CMP R11, #57          ; ASCII '9'
    JP parse_number_done  ; Not a digit - we're done

    ; Convert digit and add to result
    SUB R11, #48          ; ASCII '0' = 48
    
    ; result = result * 10 + digit
    LOAD R12, #10
    MUL R0, R12
    ADD R0, R11
    
    INC R8                ; Next character
    JMP parse_number_loop

parse_number_done:
    RET

do_mem_cmd:
    ; Get memory information with syscall 23
    SYSCALL #23
    MOVE R12, R0          ; Total memory
    MOVE R13, R6          ; Free heap bytes
    MOVE R14, R7          ; Largest free heap block
    
    LOAD R0, mem_total_msg
    SYSCALL #2
    MOVE R0, R12
    SYSCALL #1
    LOAD R0, bytes_msg
    SYSCALL #2
    
    LOAD R0, heap_free_msg
    SYSCALL #2
    MOVE R0, R13
    SYSCALL #1
    LOAD R0, bytes_msg
    SYSCALL #2
    
    LOAD R0, heap_largest_msg
    SYSCALL #2
    MOVE R0, R14
    SYSCALL #1
    LOAD R0, bytes_msg
    SYSCALL #2
    JMP parse_cmd_done
    
; Command matching function
; R5 = address of command to check
; R7 = address of input string
; Returns 1 in R0 if command matches, 0 if not
cmd_match:
    PUSH R5               ; Save command address
    PUSH R7               ; Save input address
    
cmd_match_loop:
    LOADB R11, [R5]       ; Load command character
    LOADB R12, [R7]       ; Load input character
    
    ; Check if we're at the end of the command
    CMP R11, #0
    JZ cmd_match_end_cmd
    
    ; Check if input character is space or null (end of input word)
    CMP R12, #32          ; Space
    JZ cmd_match_end_input
    CMP R12, #0           ; Null
    JZ cmd_match_end_input
    
    ; Compare characters
    CMP R11, R12
    JNZ cmd_match_not_equal
    
    ; Characters match, continue to next
    INC R5
    INC R7
    JMP cmd_match_loop
    
cmd_match_end_cmd:
    ; We reached the end of the command
    ; Check if input is also at word boundary (space or null)
    LOADB R12, [R7]
    CMP R12, #32          ; Space
    JZ cmd_match_equal
    CMP R12, #0           ; Null
    JZ cmd_match_equal
    JMP cmd_match_not_equal
    
cmd_match_end_input:
    ; We reached a word boundary in input
    ; Check if command is also finished
    LOADB R11, [R5]
    CMP R11, #0
    JZ cmd_match_equal
    
cmd_match_not_equal:
    ; Not a match
    LOAD R0, #0
    POP R7
    POP R5
    RET
    
cmd_match_equal:
    ; Commands match
    LOAD R0, #1
    POP R7
    POP R5
    RET

parse_cmd_done:
    RET

.data
welcome_msg:
    .asciiz "Welcome to Command Processor\n"
prompt_msg:
    .asciiz "\n$ "
help_text:
    .asciiz "Available commands:\n  help - Show help\n  exit - Quit\n  echo [text] - Output the text\n  clear - Clear the screen\n  time - Show system time\n  calc [expr] - Calculate expression (supports +,-,*,/)\n  mem - Show memory information\n"
unknown_cmd:
    .asciiz "Unknown command. Type 'help' for help.\n"
help_cmd:
    .asciiz "help"
exit_cmd:
    .asciiz "exit"
echo_cmd:
    .asciiz "echo"
clear_cmd:
    .asciiz "clear"
time_cmd:
    .asciiz "time"
calc_cmd:
    .asciiz "calc"
mem_cmd:
    .asciiz "mem"
time_msg:
    .asciiz "Current system time: "
seconds_msg:
    .asciiz " seconds\n"
calc_usage_msg:
    .asciiz "Usage: calc [expression]\nExample: calc 5 + 3\n"
calc_result_msg:
    .asciiz "Result: "
calc_invalid_msg:
    .asciiz "Invalid expression format\n"
calc_div_zero_msg:
    .asciiz "Error: Division by zero\n"
mem_total_msg:
    .asciiz "Total memory: "
bytes_msg:
    .asciiz " bytes\n"
heap_free_msg:
    .asciiz "Free heap: "
heap_largest_msg:
    .asciiz "Largest free heap block: "
goodbye_msg:
    .asciiz "Goodbye!\n"
input_buffer:
    .space 100