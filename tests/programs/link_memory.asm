; vmld -m records the memory that the linked program asks for
; link: modules/lines.asm
; ld-args: -m 256

.extern print_line

.text
main:
    SYSCALL #23
    SYSCALL #1
    LOAD R0, #'\n'
    SYSCALL #0
    LOAD R0, [message]
    CALL print_line
    HALT

.data
message:
    .dword text
text:
    .asciiz "linked"
