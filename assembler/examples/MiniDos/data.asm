; data.asm - Data segment definitions for MiniDOS
; Contains all string constants and buffers

.data
welcome_msg:
    .asciiz "MiniDOS v1.0\nCopyright (c) 2025\nType 'help' for commands\n"
prompt:
    .asciiz "\nA:\\> "
help_text:
    .asciiz "Available commands:\n  help - Show this help\n  cls - Clear screen\n  echo [text] - Display text\n  exit - Quit OS\n  time - Show system time\n  mem - Show memory info\n  ver - Show version\n  pause - Wait for key press\n  color [num] - Change text color (0-7)\n"
cmd_not_found:
    .asciiz "Bad command or file name\n"
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
mem_code_msg:
    .asciiz "Code segment: 0x0000-0x3FFF (16KB)\n"
mem_data_msg:
    .asciiz "Data segment: 0x4000-0x7FFF (16KB)\n"
mem_stack_msg:
    .asciiz "Stack segment: 0x8000-0xBFFF (16KB)\n"
mem_heap_msg:
    .asciiz "Heap segment: 0xC000-0xFFFF (16KB)\n"
bytes_suffix:
    .asciiz " bytes\n"
kb_suffix:
    .asciiz " KB\n"

; ----- Buffers -----
input_buffer:
    .space 256
command_buffer:
    .space 32