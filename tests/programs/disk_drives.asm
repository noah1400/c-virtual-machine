; Each -b attaches the image of the next drive, which port 0x75 selects, and vm creates a missing image empty.
; Command 3 gives the selected drive the count as its size, and a transfer of no sectors tells whether the
; drive has an image at all.
; vm-args: -b drives_a.img -b drives_b.img

.equ DISK_SECTOR,  0x70
.equ DISK_BUFFER,  0x71
.equ DISK_COUNT,   0x72
.equ DISK_COMMAND, 0x73
.equ DISK_SIZE,    0x74
.equ DISK_DRIVE,   0x75
.equ READ,         1
.equ WRITE,        2
.equ RESIZE,       3

.text
    ; Both images are new and empty, and drive 2 has none
    LOAD R8, #0
    CALL size_of
    LOAD R8, #1
    CALL size_of
    LOAD R8, #2
    CALL size_of
    LOAD R8, #0
    OUT #DISK_COUNT, R8
    LOAD R8, #READ
    CALL command
    LOAD R8, #1
    OUT #DISK_DRIVE, R8
    LOAD R8, #READ
    CALL command

    ; Drive 1 gets 2 sectors and drive 0 gets 4
    LOAD R8, #2
    OUT #DISK_COUNT, R8
    LOAD R8, #RESIZE
    CALL command
    LOAD R8, #0
    OUT #DISK_DRIVE, R8
    LOAD R8, #4
    OUT #DISK_COUNT, R8
    LOAD R8, #RESIZE
    CALL command
    LOAD R8, #0
    CALL size_of
    LOAD R8, #1
    CALL size_of

    ; A sector goes out to each drive and comes back
    LOAD R8, #0
    OUT #DISK_DRIVE, R8
    LOAD R8, #1
    OUT #DISK_COUNT, R8
    LOAD R8, #3
    OUT #DISK_SECTOR, R8
    LOAD R8, first
    OUT #DISK_BUFFER, R8
    LOAD R8, #WRITE
    CALL command
    LOAD R8, #1
    OUT #DISK_DRIVE, R8
    LOAD R8, #1
    OUT #DISK_SECTOR, R8
    LOAD R8, second
    OUT #DISK_BUFFER, R8
    LOAD R8, #WRITE
    CALL command
    LOAD R8, incoming
    OUT #DISK_BUFFER, R8
    LOAD R8, #READ
    CALL command
    LOAD R0, incoming
    SYSCALL #2
    LOAD R8, #0
    OUT #DISK_DRIVE, R8
    LOAD R8, #3
    OUT #DISK_SECTOR, R8
    LOAD R8, #READ
    CALL command
    LOAD R0, incoming
    SYSCALL #2

    ; Drive 1 has no sector 3, and drive 0 loses it when it shrinks to one sector
    LOAD R8, #1
    OUT #DISK_DRIVE, R8
    LOAD R8, #READ
    CALL command
    LOAD R8, #0
    OUT #DISK_DRIVE, R8
    LOAD R8, #1
    OUT #DISK_COUNT, R8
    LOAD R8, #RESIZE
    CALL command
    LOAD R8, #READ
    CALL command
    IN R8, #DISK_SIZE
    CALL print_int

    ; A disk holds at most 2 GB, and drive 2 cannot be resized
    LOAD R8, #0x400001
    OUT #DISK_COUNT, R8
    LOAD R8, #RESIZE
    CALL command
    LOAD R8, #2
    OUT #DISK_DRIVE, R8
    LOAD R8, #RESIZE
    CALL command
    IN R8, #DISK_DRIVE
    CALL print_int
    HALT

; Prints the size of the drive in R8
size_of:
    OUT #DISK_DRIVE, R8
    IN R8, #DISK_SIZE
    JMP print_int

; Runs the command in R8 and prints its result
command:
    OUT #DISK_COMMAND, R8
    IN R8, #DISK_COMMAND
    JMP print_int

.include "lib.inc"

.data
first:
    .asciiz "first drive\n"
    .space 499
second:
    .asciiz "second drive\n"
    .space 498
incoming:
    .space 512
