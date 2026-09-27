; Control registers are read with MFCR and written with MTCR, by name or by number

.text
    LOAD R8, #0x1234
    MTCR IVTB, R8
    MFCR R9, IVTB
    MOVE R8, R9
    CALL print_hex
    MFCR R8, #0
    CALL print_hex
    HALT

.include "lib.inc"
