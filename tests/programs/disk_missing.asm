; Without an image the disk has no sectors and every transfer fails

.equ DISK_COMMAND, 0x73
.equ DISK_SIZE,    0x74

.text
    IN R8, #DISK_SIZE
    CALL print_int
    LOAD R8, #1
    OUT #DISK_COMMAND, R8
    IN R8, #DISK_COMMAND
    CALL print_int
    HALT

.include "lib.inc"
