; expect-exit: 1
; expect-stderr: Double free detected at 0xC008
.include "../../assembler/examples/memory_double_free_test.asm"
