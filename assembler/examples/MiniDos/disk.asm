; disk.asm - A small file system for MiniDOS on the disk device
;
; Sector 0 holds the signature and sectors 1 and 2 a directory of 32 entries of 32 bytes: a name of
; up to 23 characters and a NUL, then the size in bytes. File n owns the 8 sectors from sector
; 3 + 8 * n on, so it holds up to 4096 bytes.

.equ DISK_SECTOR,     0x70
.equ DISK_BUFFER,     0x71
.equ DISK_COUNT,      0x72
.equ DISK_COMMAND,    0x73
.equ DISK_SIZE,       0x74
.equ DISK_READ,       1
.equ DISK_WRITE,      2

.equ FS_SIGNATURE,    0x5346444D        ; "MDFS"
.equ FS_ENTRIES,      32
.equ FS_ENTRY_SIZE,   32
.equ FS_NAME_SIZE,    24                ; the size follows the name
.equ FS_FILE_SECTORS, 8
.equ FS_DATA_START,   3
.equ FS_SECTORS,      FS_DATA_START + FS_ENTRIES * FS_FILE_SECTORS
.equ FS_FILE_MAX,     FS_FILE_SECTORS * 512

; ----- Commands -----

do_format_cmd:
    IN R0, #DISK_SIZE
    CMP R0, #0
    JZ fs_no_disk
    CMP R0, #FS_SECTORS
    JB fs_too_small

    ; An empty directory behind the signature
    LOAD R0, directory
    LOAD R5, #0
    MEMSET R0, R5, #FS_DATA_START * 512
    LOAD R5, #FS_SIGNATURE
    STORE R5, [directory]
    CALL save_directory
    CMP R0, #0
    JNZ parse_cmd_done
    LOAD R0, formatted_text
    JMP fs_report

do_dir_cmd:
    CALL load_directory
    CMP R0, #0
    JNZ parse_cmd_done
    LOAD R8, directory + 512
    LOAD R9, #0
    LOAD R10, #FS_ENTRIES
dir_entry:
    LOADB R0, [R8]
    CMP R0, #0
    JZ dir_next
    INC R9
    MOVE R0, R8
    LOAD R7, #FS_NAME_SIZE
    CALL print_padded
    LOAD R0, [R8+FS_NAME_SIZE]
    SYSCALL #1
    LOAD R0, bytes_suffix
    SYSCALL #2
dir_next:
    ADD R8, #FS_ENTRY_SIZE
    DEC R10
    JNZ dir_entry
    MOVE R0, R9
    SYSCALL #1
    LOAD R0, files_text
    JMP fs_report

do_type_cmd:
    CALL load_directory
    CMP R0, #0
    JNZ parse_cmd_done
    CALL parse_name
    CMP R0, #0
    JNZ parse_cmd_done
    CALL find_file
    CMP R0, #0
    JZ fs_not_found
    MOVE R8, R0
    CALL read_file
    CMP R0, #0
    JNZ parse_cmd_done
    LOAD R0, #1
    LOAD R5, file_buffer
    LOAD R6, [R8+FS_NAME_SIZE]
    SYSCALL #13
    JMP parse_cmd_done

; write NAME TEXT replaces the file with the text and a newline; append NAME TEXT adds them to it
do_write_cmd:
    LOAD R12, #0
    JMP write_text
do_append_cmd:
    LOAD R12, #1
write_text:
    CALL load_directory
    CMP R0, #0
    JNZ parse_cmd_done
    CALL parse_name
    CMP R0, #0
    JNZ parse_cmd_done
    LOAD R13, #0
    CALL find_file
    MOVE R8, R0
    CMP R8, #0
    JNZ write_existing

    ; A new file takes the first unused entry
    CALL free_entry
    MOVE R8, R0
    CMP R8, #0
    JZ fs_full
    MOVE R7, R8
write_copy_name:
    LOADB R5, [R6]
    STOREB R5, [R7]
    INC R6
    INC R7
    CMP R5, #0
    JNZ write_copy_name
    JMP write_add

write_existing:
    CMP R12, #0
    JZ write_add
    CALL read_file
    CMP R0, #0
    JNZ parse_cmd_done
    LOAD R13, [R8+FS_NAME_SIZE]

    ; The text and a newline go after the R13 bytes the file keeps
write_add:
    MOVE R6, R14
    LOAD R9, #0
write_measure:
    LOADB R5, [R6]
    CMP R5, #0
    JZ write_measured
    INC R6
    INC R9
    JMP write_measure
write_measured:
    MOVE R10, R13
    ADD R10, R9
    INC R10
    CMP R10, #FS_FILE_MAX
    JA fs_too_big
    LOAD R7, file_buffer
    ADD R7, R13
    MEMCPY R7, R14, R9
    ADD R7, R9
    LOAD R5, #10
    STOREB R5, [R7]
    STORE R10, [R8+FS_NAME_SIZE]
    CALL write_file
    CMP R0, #0
    JNZ parse_cmd_done
    CALL save_directory
    JMP parse_cmd_done

do_del_cmd:
    CALL load_directory
    CMP R0, #0
    JNZ parse_cmd_done
    CALL parse_name
    CMP R0, #0
    JNZ parse_cmd_done
    CALL find_file
    CMP R0, #0
    JZ fs_not_found
    LOAD R5, #0
    STOREB R5, [R0]
    CALL save_directory
    JMP parse_cmd_done

fs_no_disk:
    LOAD R0, no_disk_text
    JMP fs_report
fs_too_small:
    LOAD R0, too_small_text
    JMP fs_report
fs_not_found:
    LOAD R0, not_found_text
    JMP fs_report
fs_full:
    LOAD R0, directory_full_text
    JMP fs_report
fs_too_big:
    LOAD R0, too_big_text
fs_report:
    SYSCALL #2
    JMP parse_cmd_done

; ----- File system helpers -----

; Cuts the file name out of the arguments at R14
; Output: R0 = 0, R6 = the name and R14 = the text after it, or R0 = 1 after printing the problem
parse_name:
    MOVE R6, R14
    CALL skip_whitespace
    MOVE R8, R6
    CALL find_word_end
    MOVE R9, R6
    SUB R9, R8
    JZ parse_name_missing
    CMP R9, #FS_NAME_SIZE
    JAE parse_name_long
    LOADB R10, [R6]
    LOAD R11, #0
    STOREB R11, [R6]
    CMP R10, #0
    JZ parse_name_done
    INC R6
    CALL skip_whitespace
parse_name_done:
    MOVE R14, R6
    MOVE R6, R8
    LOAD R0, #0
    RET
parse_name_missing:
    LOAD R0, missing_name_text
    JMP parse_name_fail
parse_name_long:
    LOAD R0, long_name_text
parse_name_fail:
    SYSCALL #2
    LOAD R0, #1
    RET

; Reads the directory
; Output: R0 = 0 if the disk holds a file system, otherwise R0 = 1 after printing why not
load_directory:
    IN R0, #DISK_SIZE
    CMP R0, #0
    JZ load_dir_no_disk
    LOAD R0, #DISK_READ
    LOAD R5, #0
    LOAD R6, #FS_DATA_START
    LOAD R7, directory
    CALL disk_checked
    CMP R0, #0
    JNZ load_dir_done
    LOAD R5, [directory]
    CMP R5, #FS_SIGNATURE
    JZ load_dir_done
    LOAD R0, unformatted_text
    JMP load_dir_fail
load_dir_no_disk:
    LOAD R0, no_disk_text
load_dir_fail:
    SYSCALL #2
    LOAD R0, #1
load_dir_done:
    RET

save_directory:
    LOAD R0, #DISK_WRITE
    LOAD R5, #0
    LOAD R6, #FS_DATA_START
    LOAD R7, directory
    JMP disk_checked

; Moves the file of the entry at R8 between the disk and file_buffer
; Output: R0 = 0, or R0 = 1 after printing the problem
read_file:
    LOAD R0, #DISK_READ
    JMP file_transfer
write_file:
    LOAD R0, #DISK_WRITE
file_transfer:
    LOAD R6, [R8+FS_NAME_SIZE]
    ADD R6, #511
    SHR R6, #9
    ; The entry offset divided by the entry size, times the sectors per file
    MOVE R5, R8
    SUB R5, #directory + 512
    SHR R5, #5
    SHL R5, #3
    ADD R5, #FS_DATA_START
    LOAD R7, file_buffer

; Runs disk command R0 on R6 sectors from sector R5 on with the buffer at R7
; Output: R0 = 0, or R0 = 1 after printing the problem
disk_checked:
    OUT #DISK_SECTOR, R5
    OUT #DISK_BUFFER, R7
    OUT #DISK_COUNT, R6
    OUT #DISK_COMMAND, R0
    IN R0, #DISK_COMMAND
    CMP R0, #0
    JZ disk_checked_done
    LOAD R0, disk_error_text
    SYSCALL #2
    LOAD R0, #1
disk_checked_done:
    RET

; Input: R6 = file name
; Output: R0 = its directory entry, or 0
find_file:
    LOAD R7, directory + 512
    LOAD R9, #FS_ENTRIES
find_file_loop:
    LOADB R0, [R7]
    CMP R0, #0
    JZ find_file_next
    CALL strcmp
    CMP R0, #0
    JZ find_file_found
find_file_next:
    ADD R7, #FS_ENTRY_SIZE
    DEC R9
    JNZ find_file_loop
    LOAD R0, #0
    RET
find_file_found:
    MOVE R0, R7
    RET

; Output: R0 = the first unused directory entry, or 0 if the directory is full
free_entry:
    LOAD R0, directory + 512
    LOAD R9, #FS_ENTRIES
free_entry_loop:
    LOADB R7, [R0]
    CMP R7, #0
    JZ free_entry_done
    ADD R0, #FS_ENTRY_SIZE
    DEC R9
    JNZ free_entry_loop
    LOAD R0, #0
free_entry_done:
    RET

; Prints the string at R0 and pads it with spaces to R7 columns
print_padded:
    MOVE R11, R0
    SYSCALL #2
print_padded_measure:
    LOADB R0, [R11]
    CMP R0, #0
    JZ print_padded_pad
    INC R11
    DEC R7
    JMP print_padded_measure
print_padded_pad:
    CMP R7, #0
    JLE print_padded_done
    LOAD R0, #32
    SYSCALL #0
    DEC R7
    JMP print_padded_pad
print_padded_done:
    RET

.data
cmd_format:
    .asciiz "format"
cmd_dir:
    .asciiz "dir"
cmd_type:
    .asciiz "type"
cmd_write:
    .asciiz "write"
cmd_append:
    .asciiz "append"
cmd_del:
    .asciiz "del"
formatted_text:
    .asciiz "Disk formatted\n"
files_text:
    .asciiz " file(s)\n"
no_disk_text:
    .asciiz "No disk\n"
too_small_text:
    .asciiz "Disk too small\n"
unformatted_text:
    .asciiz "Disk not formatted\n"
disk_error_text:
    .asciiz "Disk error\n"
not_found_text:
    .asciiz "File not found\n"
directory_full_text:
    .asciiz "Directory full\n"
too_big_text:
    .asciiz "File too big\n"
missing_name_text:
    .asciiz "File name missing\n"
long_name_text:
    .asciiz "File name too long\n"

.align 4
directory:
    .space FS_DATA_START * 512
file_buffer:
    .space FS_FILE_MAX
