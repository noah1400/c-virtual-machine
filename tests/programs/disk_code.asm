; Code that the disk reads over code which already ran: the VM runs what the disk brought
; disk-sectors: 4

.equ DISK_SECTOR,  0x70
.equ DISK_BUFFER,  0x71
.equ DISK_COUNT,   0x72
.equ DISK_COMMAND, 0x73
.equ READ,         1
.equ WRITE,        2

.text
    ; Sector 0 gets a routine that returns 1, sector 1 one that returns 2
    LOAD R6, #0
    LOAD R7, one
    LOAD R8, #WRITE
    CALL transfer
    LOAD R6, #1
    LOAD R7, two
    CALL transfer

    ; The same buffer runs the routine of sector 0 and then that of sector 1
    LOAD R6, #0
    LOAD R7, buffer
    LOAD R8, #READ
    CALL transfer
    CALL buffer
    MOVE R8, R0
    CALL print_int
    LOAD R6, #1
    LOAD R7, buffer
    LOAD R8, #READ
    CALL transfer
    CALL buffer
    MOVE R8, R0
    CALL print_int
    HALT

; Moves sector R6 from or to the buffer at R7 with the command in R8
transfer:
    OUT #DISK_SECTOR, R6
    OUT #DISK_BUFFER, R7
    LOAD R0, #1
    OUT #DISK_COUNT, R0
    OUT #DISK_COMMAND, R8
    RET

one:
    LOAD R0, #1
    RET
two:
    LOAD R0, #2
    RET

.include "lib.inc"

.data
buffer:
    .space 512
