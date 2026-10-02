; The assembly behind std/sys
    .library

.text
supervisor:
    LOAD R0, #4
    CPUID
    MOVE R0, R5
    SHR R0, #3
    AND R0, #1
    RET
