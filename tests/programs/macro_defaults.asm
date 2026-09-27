; Macro parameters can have defaults, used when an argument is left out or empty

.macro SHOW value, base=10, suffix='\n'
    LOAD R0, #\value
    LOAD R5, #\base
    SYSCALL #6
    LOAD R0, #\suffix
    SYSCALL #0
.endm

.text
    SHOW 255
    SHOW 255, 16
    SHOW 255, 2, ' '
    SHOW 7, , '!'
    LOAD R0, #'\n'
    SYSCALL #0
    HALT
