; expect-error: undefined symbol 'nowhere'
.text
start:
    JMP nowhere
    LOAD R0, [elsewhere]
next:
    JMP start
