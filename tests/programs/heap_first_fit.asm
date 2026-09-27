; First fit puts a block in the lowest gap that holds it, after the 8-byte guard of the block before
; the next one, and at the end of the heap when no gap does. Addresses print relative to the first block.

.text
    ALLOC R14, #64
    ALLOC R9, #16
    ALLOC R10, #64
    ALLOC R11, #16
    ALLOC R12, #64
    ALLOC R13, #16
    FREE R14
    FREE R10
    FREE R12

    ; Gaps of 64 bytes where the 64-byte blocks were: the first one takes 40 and 16 bytes
    ALLOC R8, #40
    CALL print_offset
    ALLOC R8, #16
    CALL print_offset
    ; 8 bytes no longer fit before the second block, so they go to the next gap
    ALLOC R8, #8
    CALL print_offset
    ; What is left of that gap is too small for 64 bytes, and the third gap is not
    ALLOC R8, #64
    CALL print_offset
    ; A lower gap wins over the end of the heap
    ALLOC R8, #48
    CALL print_offset
    ALLOC R8, #8
    CALL print_offset

    SYSCALL #23
    MOVE R8, R6
    CALL print_int
    MOVE R8, R7
    CALL print_int
    HALT

print_offset:
    SUB R8, R14
    CALL print_int
    RET

.include "lib.inc"
