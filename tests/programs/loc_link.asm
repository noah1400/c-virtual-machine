; The lines that .loc named in an object file keep their addresses when vmld links it
; link: modules/fact.asm
; expect-exit: 1
    .extern fact

.text
main:
    PUSH #3
    CALL fact
    HALT
