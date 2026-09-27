; Linking fails when no object file exports a symbol that another one uses
; link: modules/lines.asm
; expect-link-error: undefined symbol 'missing'

.extern print_line, missing

.text
    CALL print_line
    CALL missing
    HALT
