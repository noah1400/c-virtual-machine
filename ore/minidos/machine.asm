; The assembly behind machine.ore
.text
peek:
    LOAD R6, [SP+4]
    LOAD R0, [R6]
    RET

memory_info:
    SYSCALL #23
    LOAD R5, [SP+4]
    STORE R0, [R5]
    STORE R6, [R5+24]
    STORE R7, [R5+28]
    LOAD R0, #0
    STORE R0, [R5+20]
    RET

free_heap:
    SYSCALL #23
    MOVE R0, R6
    RET
