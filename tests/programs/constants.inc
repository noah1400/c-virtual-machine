; Included by assembler.asm; the section it switches to does not leak back
.data
included_text:
    .asciiz "from an included file"
