; expect-error: expected a line number after the file name
.text
    .inline "f.c"
    HALT
    .endinline
