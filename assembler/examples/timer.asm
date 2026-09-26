; Lets the timer interrupt the main loop every 1000 instructions until five ticks arrived

.equ TIMER_INTERVAL, 0x40
.equ TIMER_VECTOR,   0x41
.equ TIMER_TICKS,    0x42
.equ VECTOR,         0x20

.entry main

.text
.org 0x0100 + VECTOR * 4
    .dword on_timer

main:
    LOAD R8, #VECTOR
    OUT #TIMER_VECTOR, R8
    LOAD R8, #1000
    OUT #TIMER_INTERVAL, R8
    STI

wait:
    LOAD R9, [ticks]
    CMP R9, #5
    JB wait

    CLI
    LOAD R8, #0
    OUT #TIMER_INTERVAL, R8
    IN R9, #TIMER_TICKS
    LOAD R0, done_text
    SYSCALL #2
    MOVE R0, R9
    SYSCALL #1
    LOAD R0, #'\n'
    SYSCALL #0
    HALT

; The CPU saves every register on entry and IRET restores them
on_timer:
    LOAD R8, [ticks]
    INC R8
    STORE R8, [ticks]
    LOAD R0, #'.'
    SYSCALL #0
    IRET

.data
ticks:
    .dword 0
done_text:
    .asciiz "\nTimer ticks counted by the device: "
