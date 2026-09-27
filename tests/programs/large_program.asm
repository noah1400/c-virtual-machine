; Code and data may be far larger than 64 KB, reached through extension words

.text
    LOAD R8, [after_buffer]
    CALL print_int
    LOAD R8, #after_buffer - buffer
    CALL print_int
    LOAD R8, #buffer
    AND R8, #VM_PAGE_MASK
    CALL print_int              ; data starts on a page boundary
    JMP far_code
    .space 70000
far_code:
    LOAD R8, #far_code
    SHR R8, #16
    CALL print_int
    HALT

.equ VM_PAGE_MASK, 0xFFF

.include "lib.inc"

.data
buffer:
    .space 100000
after_buffer:
    .dword 12345
