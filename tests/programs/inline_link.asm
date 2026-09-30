; The calls that .inline named in an object file show in backtraces once vmld links it, innermost first
; link: modules/shapes.asm
; expect-exit: 1
    .extern area

.text
main:
    PUSH #0
    PUSH #6
    CALL area
    HALT
