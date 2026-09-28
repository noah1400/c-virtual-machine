; Routines for library.asm, which uses only some of them
    .library

.text
double:
    ADD R8, R8
increment:
    INC R8
    RET

unused:
    CALL missing
    CALL also_unused
    RET

also_unused:
    LOAD R8, [unused_text]
    RET

answer:
    NOP
.load:
    LOAD R8, [value]
    RET

.data
value:
    .dword 42
unused_text:
    .asciiz "never used"
