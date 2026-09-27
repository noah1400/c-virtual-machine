; vmld -b places the linked program at another address than 0, and fills in addresses for it
; link: modules/lines.asm
; ld-args: -b 0x20000

.extern print_line

.text
main:
    LOAD R0, main
    SYSCALL #5
    LOAD R0, #'\n'
    SYSCALL #0
    LOAD R0, [message]
    CALL print_line
    LOAD R0, message
    SYSCALL #5
    LOAD R0, #'\n'
    SYSCALL #0
    HALT

.data
message:
    .dword text
text:
    .asciiz "linked high"
