; The assembly of native.ore, which comes along with any program that imports it
.text
rotate_left:
    LOAD R0, [SP+4]
    LOAD R6, [SP+8]
    ROL R0, R6
    RET

count_bits:
    LOAD R6, [SP+4]
    POPCNT R0, R6
    RET
