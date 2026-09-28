; .loc without the text of the line takes it from the file, when vmasm can read that
; vm-args: -c -
.text
    .loc "tests/programs/loc_source.txt", 2
    LOAD R0, #7
    .loc "tests/programs/loc_source.txt", 3
    SYSCALL #1
    .loc "tests/programs/missing.txt", 1
    HALT
