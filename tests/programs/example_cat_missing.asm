; Runs assembler/examples/cat.asm on a file that does not exist
; program-args: missing.txt
; expect-exit: 1
; expect-stderr: cat: cannot open missing.txt
.include "../../assembler/examples/cat.asm"
