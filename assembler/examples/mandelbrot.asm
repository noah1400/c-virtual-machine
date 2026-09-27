; Draws the Mandelbrot set in characters using the floating point instructions

.equ WIDTH, 48
.equ HEIGHT, 20
.equ LIMIT, 16

.text
    LOAD R8, #0
.row:
    ITOF R10, R8
    FMUL R10, #0.1
    FSUB R10, #1.0              ; imaginary part of c
    LOAD R9, #0
.column:
    ITOF R11, R9
    FMUL R11, #0.0625
    FSUB R11, #2.0              ; real part of c
    LOAD R12, #0                ; z starts at 0.0, whose bits are 0
    LOAD R13, #0
    LOAD R14, #0

    ; z = z * z + c until |z| exceeds 2 or LIMIT steps have passed
.iterate:
    MOVE R5, R12
    FMUL R5, R12
    MOVE R6, R13
    FMUL R6, R13
    MOVE R7, R5
    FADD R7, R6
    FCMP R7, #4.0
    JA .escaped
    FMUL R13, R12
    FADD R13, R13
    FADD R13, R10
    MOVE R12, R5
    FSUB R12, R6
    FADD R12, R11
    INC R14
    CMP R14, #LIMIT
    JB .iterate

.escaped:
    LOAD R0, #'#'
    CMP R14, #LIMIT
    JAE .print
    SHR R14, #1
    LOADB R0, [R14 + palette]
.print:
    SYSCALL #0
    INC R9
    CMP R9, #WIDTH
    JB .column
    LOAD R0, #'\n'
    SYSCALL #0
    INC R8
    CMP R8, #HEIGHT
    JB .row
    HALT

.data
palette:
    .ascii " .:-=+*%"
