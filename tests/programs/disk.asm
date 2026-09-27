; The disk moves whole sectors between its image and memory, and reports what went wrong
; disk-sectors: 8

.equ DISK_SECTOR,  0x70
.equ DISK_BUFFER,  0x71
.equ DISK_COUNT,   0x72
.equ DISK_COMMAND, 0x73
.equ DISK_SIZE,    0x74
.equ READ,         1
.equ WRITE,        2

.text
    IN R8, #DISK_SIZE
    CALL print_int

    ; Two sectors go out from sector 3 on, and the second one comes back
    LOAD R8, #3
    OUT #DISK_SECTOR, R8
    LOAD R8, outgoing
    OUT #DISK_BUFFER, R8
    LOAD R8, #2
    OUT #DISK_COUNT, R8
    LOAD R8, #WRITE
    CALL command
    LOAD R8, #4
    OUT #DISK_SECTOR, R8
    LOAD R8, incoming
    OUT #DISK_BUFFER, R8
    LOAD R8, #1
    OUT #DISK_COUNT, R8
    LOAD R8, #READ
    CALL command
    LOAD R0, incoming
    SYSCALL #2

    ; A transfer past the end of the disk, an unknown command and a buffer past the end of memory
    LOAD R8, #7
    OUT #DISK_SECTOR, R8
    LOAD R8, #2
    OUT #DISK_COUNT, R8
    LOAD R8, #READ
    CALL command
    LOAD R8, #9
    CALL command
    LOAD R8, #0
    OUT #DISK_SECTOR, R8
    LOAD R8, #0xFFFFFF00
    OUT #DISK_BUFFER, R8
    LOAD R8, #READ
    CALL command
    HALT

; Runs the command in R8 and prints its result
command:
    OUT #DISK_COMMAND, R8
    IN R8, #DISK_COMMAND
    JMP print_int

.include "lib.inc"

.data
outgoing:
    .asciiz "first sector\n"
    .space 512 - 14
    .asciiz "second sector\n"
    .space 512 - 15
incoming:
    .space 512
