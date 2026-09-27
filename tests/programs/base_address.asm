; -b puts a program at another address than 0: its labels, its data and its entry point move with it
; asm-args: -b 0x10000
    .entry start

start:
    LOAD R0, start
    CALL show_hex
    LOAD R0, [value]
    SYSCALL #1
    CALL newline
    LOAD R0, [pointer]
    CALL show_hex
    LOAD R6, [table]
    CALL R6
    HALT

show_hex:
    SYSCALL #5
newline:
    LOAD R0, #10
    SYSCALL #0
    RET

called:
    LOAD R0, called
    JMP show_hex

.data
value:      .dword 42
pointer:    .dword value
table:      .dword called
