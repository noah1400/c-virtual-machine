; A tour of the console, memory and random number syscalls

.text
    LOAD R0, console_text
    SYSCALL #2

    LOAD R0, #'A'
    SYSCALL #0                  ; print character
    LOAD R0, #' '
    SYSCALL #0
    LOAD R0, #1234
    SYSCALL #1                  ; print signed integer
    LOAD R0, #' '
    SYSCALL #0
    LOAD R0, #0xBEEF
    SYSCALL #5                  ; print hexadecimal
    LOAD R0, #' '
    SYSCALL #0
    LOAD R0, #10
    LOAD R5, #2
    SYSCALL #6                  ; print in base 2
    LOAD R0, #' '
    SYSCALL #0
    LOAD R0, [pi]
    SYSCALL #7                  ; print a float
    LOAD R0, #'\n'
    SYSCALL #0

    LOAD R0, memory_text
    SYSCALL #2
    LOAD R0, #32
    SYSCALL #20                 ; allocate 32 bytes
    MOVE R8, R0
    LOAD R5, greeting
    LOAD R6, #greeting_end - greeting
    SYSCALL #22                 ; copy the greeting into the block
    MOVE R0, R8
    SYSCALL #2
    MOVE R0, R8
    SYSCALL #21                 ; free it again

    SYSCALL #23                 ; memory information
    MOVE R9, R6
    LOAD R0, free_text
    SYSCALL #2
    MOVE R0, R9
    SYSCALL #1
    LOAD R0, #'\n'
    SYSCALL #0

    LOAD R0, random_text
    SYSCALL #2
    LOAD R0, #12345
    SYSCALL #41                 ; seed the generator
    LOAD R8, #5
roll:
    LOAD R0, #6
    SYSCALL #40                 ; random number from 0 to 5
    ADD R0, #1
    SYSCALL #1
    LOAD R0, #' '
    SYSCALL #0
    LOOP R8, roll
    LOAD R0, #'\n'
    SYSCALL #0
    HALT

.data
pi:
    .dword 0x40490FDB           ; pi as a float
console_text:
    .asciiz "Console: "
memory_text:
    .asciiz "Memory: "
greeting:
    .asciiz "copied into the heap\n"
greeting_end:
free_text:
    .asciiz "Free heap bytes: "
random_text:
    .asciiz "Five dice rolls: "
