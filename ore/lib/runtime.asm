; The runtime of Ore programs: startup, command-line arguments, memory and runtime errors. The error
; routines never return; they stop the program with syscall 35, which reports the Ore line that called
; them. As a library, it gives a program only the routines and texts that it uses; a HALT follows the
; syscalls that do not return, so that no routine seems to run on into the next.
    .library
    .entry rt.start

.text
rt.start:
    CALL main
    SYSCALL #30
    HALT

; Returns the program path and its arguments as a [][]u8, the pointer in R0 and the count in R5
rt.args:
    PUSH R8
    PUSH R9
    PUSH R10
    LOAD R8, #0
.count:
    MOVE R0, R8
    LOAD R6, #0
    SYSCALL #34
    CMP R0, #-1
    JZ .counted
    INC R8
    JMP .count
.counted:
    MOVE R0, R8
    SHL R0, #3
    PUSH R0
    CALL rt.alloc
    ADD SP, #4
    MOVE R9, R0
    LOAD R10, #0
.next:
    CMP R10, R8
    JAE .done
    MOVE R0, R10
    LOAD R6, #0
    SYSCALL #34
    MOVE R6, R10
    SHL R6, #3
    ADD R6, R9
    STORE R0, [R6+4]
    INC R0
    PUSH R0
    CALL rt.alloc
    ADD SP, #4
    MOVE R6, R10
    SHL R6, #3
    ADD R6, R9
    STORE R0, [R6]
    MOVE R5, R0
    LOAD R6, [R6+4]
    INC R6
    MOVE R0, R10
    SYSCALL #34
    INC R10
    JMP .next
.done:
    MOVE R0, R9
    MOVE R5, R8
    POP R10
    POP R9
    POP R8
    RET

; Returns zeroed memory of [SP+4] bytes in R0
rt.alloc:
    LOAD R0, [SP+4]
    SYSCALL #20
    CMP R0, #0
    JZ .none
    RET
.none:
    LOAD R8, rt.message
    LOAD R0, rt.text_memory
    CALL rt.append
    JMP rt.abort

; Frees the memory at [SP+4], if it is not null
rt.free:
    LOAD R0, [SP+4]
    CMP R0, #0
    JZ .done
    SYSCALL #21
    CMP R5, #0
    JNZ .invalid
.done:
    RET
.invalid:
    LOAD R8, rt.message
    LOAD R0, rt.text_free
    CALL rt.append
    JMP rt.abort

; index [SP+4] is out of bounds for length [SP+8]
rt.index_error:
    LOAD R8, rt.message
    LOAD R0, rt.text_index
    CALL rt.append
    LOAD R0, [SP+4]
    CALL rt.append_int
    LOAD R0, rt.text_bounds
    CALL rt.append
    LOAD R0, [SP+8]
    CALL rt.append_int
    JMP rt.abort

; slice [SP+4]:[SP+8] is out of bounds for length [SP+12], which is -1 for a pointer
rt.slice_error:
    LOAD R8, rt.message
    LOAD R0, rt.text_slice
    CALL rt.append
    LOAD R0, [SP+4]
    CALL rt.append_int
    LOAD R6, #':'
    STOREB R6, [R8]
    INC R8
    LOAD R0, [SP+8]
    CALL rt.append_int
    LOAD R0, [SP+12]
    CMP R0, #-1
    JZ .pointer
    LOAD R0, rt.text_bounds
    CALL rt.append
    LOAD R0, [SP+12]
    CALL rt.append_int
    JMP rt.abort
.pointer:
    LOAD R0, rt.text_backwards
    CALL rt.append
    JMP rt.abort

rt.null_error:
    LOAD R8, rt.message
    LOAD R0, rt.text_null
    CALL rt.append
    JMP rt.abort

rt.assert_error:
    LOAD R8, rt.message
    LOAD R0, rt.text_assert
    CALL rt.append
    JMP rt.abort

; Stops with the message the slice [SP+4] (pointer) and [SP+8] (length) holds, shortened to fit
rt.panic:
    LOAD R8, rt.message
    LOAD R0, [SP+4]
    LOAD R7, [SP+8]
    CMP R7, #200
    JBE .copy
    LOAD R7, #200
.copy:
    CMP R7, #0
    JZ rt.abort
    LOADB R6, [R0]
    STOREB R6, [R8]
    INC R0
    INC R8
    DEC R7
    JMP .copy

; Appends the NUL-terminated string at R0 to the message at R8
rt.append:
    LOADB R6, [R0]
    CMP R6, #0
    JZ .done
    STOREB R6, [R8]
    INC R0
    INC R8
    JMP rt.append
.done:
    RET

; Appends the signed number R0 to the message at R8
rt.append_int:
    CMP R0, #0
    JGE .digits
    LOAD R6, #'-'
    STOREB R6, [R8]
    INC R8
    NEG R0
.digits:
    LOAD R7, #0
.divide:
    MOVE R6, R0
    MOD R6, #10
    ADD R6, #'0'
    PUSH R6
    INC R7
    DIV R0, #10
    CMP R0, #0
    JNZ .divide
.write:
    POP R6
    STOREB R6, [R8]
    INC R8
    DEC R7
    JNZ .write
    RET

; Ends the message at R8 and stops the program, reported at the call of the error routine
rt.abort:
    LOAD R6, #0
    STOREB R6, [R8]
    LOAD R0, rt.message
    LOAD R5, #1
    SYSCALL #35
    HALT

.data
rt.true:
    .asciiz "true"
rt.false:
    .asciiz "false"
rt.text_memory:
    .asciiz "out of memory"
rt.text_free:
    .asciiz "free of memory that new did not return or that is already free"
rt.text_index:
    .asciiz "index "
rt.text_bounds:
    .asciiz " is out of bounds for length "
rt.text_slice:
    .asciiz "slice "
rt.text_backwards:
    .asciiz " ends before it starts"
rt.text_null:
    .asciiz "null pointer"
rt.text_assert:
    .asciiz "assertion failed"
rt.message:
    .space 256
