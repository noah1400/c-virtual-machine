; Code that runs compiled from the first jump to it on: flags that another block reads, calls with frames,
; stack instructions, bytes and words, a loop that rewrites itself, and an instruction limit that stops a
; compiled loop at the same instruction as without compiled code
; vm-args: -j 1 -n 3000
; expect-exit: 1

.text
    ; The compare sets the flags that the jump at the start of another block reads
    LOAD R8, #0
    LOAD R9, #10
.count:
    INC R8
    CMP R8, R9
    JMP .test
.test:
    JL .count
    CALL print_int

    ; Calls with a frame and their argument on the stack
    LOAD R8, #0
    LOAD R10, #20
.calls:
    PUSH R10
    CALL add_twice
    DEC R10
    JNZ .calls
    CALL print_int

    ; PUSHM and POPM in a loop
    LOAD R8, #3
    LOAD R9, #4
    LOAD R10, #5
    LOAD R11, #0
.saved:
    PUSHM R8, R10
    LOAD R8, #0
    LOAD R9, #0
    LOAD R10, #0
    POPM R8, R10
    INC R11
    CMP R11, #3
    JNZ .saved
    ADD R8, R9
    ADD R8, R10
    CALL print_int

    ; Bytes and words
    LOAD R8, #0
    LOAD R10, text
.bytes:
    LOADB R0, [R10]
    CMP R0, #0
    JZ .bytes_done
    ADD R8, R0
    INC R10
    JMP .bytes
.bytes_done:
    LOADW R0, [halves+2]
    ADD R8, R0
    CALL print_int

    ; A loop that raises the immediate of its own ADD each round: 1 + 2 + ... + 10
    LOAD R9, #0
    LOAD R10, #1
.add:
    ADD R9, #1
    INC R10
    LOAD R0, [.add]
    AND R0, #0xFFFF0000
    OR R0, R10
    STORE R0, [.add]
    CMP R10, #11
    JNZ .add
    MOVE R8, R9
    CALL print_int

    ; The instruction limit stops this loop
    LOAD R8, #0
.spin:
    ADD R8, #3
    SHL R8, #1
    SHR R8, #1
    JMP .spin

; Adds twice the argument to R8
add_twice:
    ENTER #4
    LOAD R0, [BP+8]
    STORE R0, [BP-4]
    ADD R8, R0
    LOAD R0, [BP-4]
    ADD R8, R0
    LEAVE
    RET #4

.include "lib.inc"

.data
text:
    .string "JIT"
halves:
    .word 7, 1000
