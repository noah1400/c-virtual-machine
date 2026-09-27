; This file lies next to the main module, where vmc0 -S would write its assembly, and must not be
; included in the program: its main would clash with the one vmc0 generates.
main:
    RET
