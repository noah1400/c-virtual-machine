; Example assembly program for the VM
; This program calculates the factorial of a number

.text                   ; Code section
    ; Initialize registers
    LOAD R5, #5         ; Number to calculate factorial of
    LOAD R0, #1         ; Initialize result to 1
    
    ; Check for special case of factorial 0 (result is 1)
    CMP R5, #0
    JZ done
    
loop:
    ; Multiply accumulator by the counter
    MUL R0, R5
    
    ; Decrement counter
    DEC R5
    
    ; Check if we're done
    CMP R5, #0
    JNZ loop            ; Continue if counter > 0
    
done:
    ; Print the result
    PUSH R0             ; Save the result
    ; Print newline
    LOAD R0, #10        ; ASCII for newline
    SYSCALL #0          ; Syscall 0 = print character

    ; Print message
    LOAD R0, message    ; Load address of message
    SYSCALL #2          ; Syscall 2 = print string

    ; Print the result
    POP R0              ; Restore the result
    SYSCALL #1          ; Syscall 1 = print integer (R0 has the value)

    ; Print newline
    LOAD R0, #10        ; ASCII for newline
    SYSCALL #0          ; Syscall 0 = print character

    ; Exit program
    HALT

.data                   ; Data section
message:
    .asciiz "Factorial result: "  ; Null-terminated string