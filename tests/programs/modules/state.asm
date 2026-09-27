; Data and constants that the linking tests share

.global counter, handlers, LIMIT
.extern print_line

.equ LIMIT, 3

.data
counter:
    .dword 40
handlers:
    .dword print_line, counter
