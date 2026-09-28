; The assembly behind std/math
    .library

.text
sqrt:
    FSQRT R0, [SP+4]
    RET

abs:
    LOAD R0, [SP+4]
    FABS R0
    RET
