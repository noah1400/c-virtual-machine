; expect-exit: 1
; expect-stderr: Double fault: Stack overflow
.text
    LOAD R8, vectors
    MTCR IVTB, R8
recurse:
    CALL recurse

handler:
    IRET

.data
vectors:
    .space 3 * 4
    .dword handler
    .space 252 * 4
