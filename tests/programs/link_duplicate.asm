; Linking fails when two object files export the same symbol
; link: modules/lines.asm
; expect-link-error: 'print_line' is also exported by

.global print_line

.text
print_line:
    HALT
