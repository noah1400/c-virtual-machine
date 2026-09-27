; data.asm - Data definitions for MiniDOS
; Contains all string constants and buffers

.data
welcome_msg:
    .asciiz "MiniDOS v1.0\nCopyright (c) 2025\nType 'help' for commands\n"
prompt:
    .asciiz "\nA:\\> "
help_text:
    .asciiz "Available commands:\n  help - Show this help\n  cls - Clear screen\n  echo [text] - Display text\n  exit - Quit OS\n  time - Show system time\n  mem - Show memory info\n  ver - Show version\n  pause - Wait for key press\n  color [num] - Change text color (0-7)\n\nWith a disk image (vm -b FILE):\n  format - Create an empty file system\n  dir - List files\n  type [file] - Show a file\n  write [file] [text] - Replace a file with a line of text\n  append [file] [text] - Add a line of text to a file\n  del [file] - Delete a file\n"
cmd_not_found:
    .asciiz "Bad command or file name\n"

; Command names and their handlers, ending with a 0
command_table:
    .dword cmd_help, do_help_cmd
    .dword cmd_cls, do_cls_cmd
    .dword cmd_echo, do_echo_cmd
    .dword cmd_exit, do_exit_cmd
    .dword cmd_time, do_time_cmd
    .dword cmd_mem, do_mem_cmd
    .dword cmd_ver, do_ver_cmd
    .dword cmd_pause, do_pause_cmd
    .dword cmd_color, do_color_cmd
    .dword cmd_format, do_format_cmd
    .dword cmd_dir, do_dir_cmd
    .dword cmd_type, do_type_cmd
    .dword cmd_write, do_write_cmd
    .dword cmd_append, do_append_cmd
    .dword cmd_del, do_del_cmd
    .dword 0
cmd_help:
    .asciiz "help"
cmd_cls:
    .asciiz "cls"
cmd_echo:
    .asciiz "echo"
cmd_exit:
    .asciiz "exit"
cmd_time:
    .asciiz "time"
cmd_mem:
    .asciiz "mem"
cmd_ver:
    .asciiz "ver"
cmd_pause:
    .asciiz "pause"
cmd_color:
    .asciiz "color"
version_text:
    .asciiz "MiniDOS Version 1.0\nCopyright (c) 2025\n"
time_text:
    .asciiz "Current system time: "
seconds_text:
    .asciiz " seconds\n"
pause_text:
    .asciiz "Press any key to continue..."
mem_total_msg:
    .asciiz "Total memory: "
mem_free_msg:
    .asciiz "Free heap: "
mem_largest_msg:
    .asciiz "Largest free block: "
mem_stack_msg:
    .asciiz "Stack size: "
bytes_suffix:
    .asciiz " bytes\n"
kb_suffix:
    .asciiz " KB\n"

; ----- Buffers -----
input_buffer:
    .space 256
command_buffer:
    .space 32
