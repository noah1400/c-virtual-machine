; .library leaves out the code and data of routines.inc that nothing uses: double and answer are
; called, increment is run into from double, value is read by answer, and the rest goes, so that
; nothing has to define missing
.text
    LOAD R8, #20
    CALL double
    CALL print_int
    CALL answer.load
    CALL print_int
    LOAD R8, #routines_end - routines
    CALL print_int
    LOAD R8, #data_end - data
    CALL print_int
    HALT

.data
data:
.text
routines:
.include "routines.inc"
routines_end:
.data
data_end:

.text
.include "lib.inc"
