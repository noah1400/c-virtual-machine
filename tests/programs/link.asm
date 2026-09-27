; Programs can be split into object files that vmld links, with symbols exported by .global
; link: modules/lines.asm modules/state.asm

.extern print_line, length, counter, handlers, LIMIT

.text
main:
    LOAD R0, greeting
    CALL print_line
    LOAD R0, greeting
    CALL length
    MOVE R8, R0
    CALL print_number

    ; Data of another file, and a table of addresses that the linker filled in
    LOAD R8, [counter]
    ADD R8, #LIMIT - 1
    STORE R8, [counter]
    LOAD R8, [counter]
    CALL print_number
    LOAD R9, [handlers + 4]
    LOAD R8, [R9]
    CALL print_number
    LOAD R0, farewell
    CALL [handlers]

    ; The difference of two labels is a number, even before they are defined
    LOAD R8, #farewell_end - farewell
    CALL print_number
    LOAD R8, #1 << (after_shift - $)
after_shift:
    CALL print_number
    HALT

print_number:
    MOVE R0, R8
    SYSCALL #1
    LOAD R0, #'\n'
    SYSCALL #0
    RET

.data
greeting:
    .asciiz "linked"
farewell:
    .asciiz "done"
farewell_end:
