# Into count, then a line at a time: the jump to the loop test stays on line 3
s 2
n
n
n
n
n
# Lines are locations too, and a file name may leave out its directories
b count.c:4
b loc_debugger.asm:8
b count.c:9
lb
c
c
ls
c
