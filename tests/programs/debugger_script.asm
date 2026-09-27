; Debugger commands come from NAME.x through -x, so the program keeps stdin for its own input

.text
main:
    LOAD R0, line
    LOAD R5, #32
    SYSCALL #4
read_done:
    LOAD R0, line
    SYSCALL #2
    LOAD R0, #'\n'
    SYSCALL #0
    HALT

.data
line:
    .space 32
