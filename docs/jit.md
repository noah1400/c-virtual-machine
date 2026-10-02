# The JIT

On x86-64 Linux and macOS, `vm` compiles the VM code that jumps go to often into x86-64 code. The JIT lives inside `cpu_run`, the loop that runs the common instructions from their decoded form, and does not replace it: compiled code covers the instructions it can run exactly and hands everything else back to `cpu_run`, which runs it as before. A program cannot tell the difference. Registers, flags, memory, the instruction count, faults and backtraces come out the same with compiled code and without. The [README](../README.md#compiled-code) says how to turn it off and how much faster programs run.

## When code gets compiled

`cpu_run` decodes every word of code into a `Decoded` entry the first time it reaches it. Each entry has a `heat` byte that starts at the threshold, 32 or what `-j` sets, and every jump to the word counts it down: `JMP`, a taken conditional jump, `CALL`, `RET`, `LOOP`, and the jumps that compiled code leaves through. At 0, `jit_compile` compiles the code from that word. So only words that jumps go to start compiled code: loop heads, functions, return addresses and branch targets. Jumps to a word that is not decoded yet count down from 0, and decoding takes them into account, so `-j 1` compiles from the very first jump.

`-j 0` never compiles. Whatever has to happen between two instructions makes `vm_run` step through them with `vm_step` instead of `cpu_run`, and then no compiled code runs either: the debugger, `-t`, `-p`, `-c`, `-H`, logpoints, paging, the trap flag, an interrupt waiting to be delivered, and the timer or a keyboard interrupt while they are on.

## Blocks

`compile_block` takes the instructions from the hot word on until one of these:

- An instruction that always goes elsewhere: `JMP` to a fixed or computed address, `CALL` or `RET`.
- An instruction it does not compile: `POPCNT`, `CLZ`, `CTZ`, the float instructions, the syscalls other than those for the heap, and the instructions that `cpu_run` leaves to `vm_step`, such as `HALT`, `IN`, `OUT` and writes to PC or SR.
- The start of other compiled code, which the block then goes straight on to.
- 256 instructions.

A conditional jump does not end a block. It goes to a stub after the block, and the instructions after the jump stay in the block. When a block ends at an instruction that it does not compile, the code after that instruction is compiled too, up to four blocks in a row, since `cpu_run` comes back there right after.

Before writing any code, a pass from the last instruction to the first marks the instructions whose flags something can read (see [Flags](#flags)). Then `compile_item` turns every instruction into x86-64 code, and `compile_stub` writes the slow paths after the block. The word the block starts at gets the kind `D_JIT`, which sends `cpu_run` into the compiled code, and its own kind moves to a table. The code goes into 32 MB of executable memory, which `jit.c` fills with its own encoder of x86-64 instructions.

## The code

| Host register | Holds |
|---|---|
| `ebx` | SP |
| `ebp` | R0 |
| `r12` | The VM's memory |
| `r13` | The VM, with the other VM registers, the control registers and everything else the checks read |
| `r14d` | The budget: how many more instructions compiled code may run before `cpu_run` has to look |
| `rsp` | A frame with 8 bytes for slow paths, the context from `cpu_run`, the table of compiled code by word and the flags |

SP and R0 are the registers that compiled Ore code uses most; the others stay in the VM. The entry code (`write_entry`) saves the host registers that C code expects to keep, loads the ones above, copies the flags into the frame and jumps to the block. Its exit writes SP, R0, the flags and the budget back and returns to `cpu_run` with the word or address to go on at.

This is the code for the start of the recursive function in `bench/fib.ore`, with the stubs and the slow paths of its checks left out:

```
; LOAD R0, [SP+4] / CMP R0, #2 / JGE .L1 / RET #4
endbr64
sub   r14d, 4                   ; the 4 instructions come out of the budget
jb    budget                    ;   too few left: cpu_run runs them one by one
mov   edx, ebx                  ; LOAD R0, [SP+4]
add   edx, 4
mov   ecx, edx                  ;   on the stack?
sub   ecx, [r13 + SLO]
cmp   ecx, [r13 + stack_span]
jb    .stack                    ;   then nothing else needs checking
...                             ;   else the size of memory and the heap
.stack:
lea   rax, [r12 + rdx]
mov   eax, [rax]
mov   ebp, eax
mov   ecx, 2                    ; CMP R0, #2
mov   eax, ebp
mov   [rsp + 0x1C], eax         ;   the flags as cpu_run keeps them: the operands
mov   [rsp + 0x20], ecx         ;   and how they were combined
mov   dword [rsp + 0x18], 2
cmp   eax, ecx                  ; JGE .L1 on the flags of this very compare
jge   taken                     ;   which jumps straight into the code at .L1
mov   ecx, ebx                  ; RET #4: the return address, checked as for a pop
...
mov   eax, [rax]
add   ebx, 8                    ;   SP above it and the argument
...                             ;   the call leaves the shadow call stack
mov   ecx, eax                  ;   the word of the return address, or a huge
ror   ecx, 2                    ;   number when it is not aligned
cmp   ecx, [r13 + decoded_words]
jae   leave                     ;   outside the code: vm_step deals with it
mov   rdx, [rsp + 0x10]         ;   compiled code at that word?
mov   rdx, [rdx + rcx*8]
test  rdx, rdx
je    next                      ;   none: cpu_run goes on there
jmp   rdx
```

## Staying exact

### Instructions

A block takes all its instructions out of the budget when it starts, and every way out of the block gives back those it did not run. When the budget does not cover a block, the block leaves before its first instruction and `cpu_run` runs that instruction itself, so an instruction limit stops a program at the same instruction with compiled code and without.

### Flags

`cpu_run` does not compute the flags after every instruction. It keeps the operands of the last instruction that set them and how they were combined (`Flags` in `run.h`), and works out a flag only when something reads it. Compiled code keeps the flags in the same form in its frame, so that `cpu_run` can take over anywhere. It stores them only where something can read them before other instructions replace them: a conditional jump, `SET`, `ADDC`, `SUBC`, a syscall, or any way out of the block. That includes every instruction that can fault, since a fault handler sees the flags in SR.

A conditional jump or `SET` right after the instruction that set its flags uses the host flags of that instruction. Further on in a block, where the compiler knows that an addition, subtraction, comparison or logic instruction set the flags last, one `cmp` or `add` of the stored operands brings them back; only otherwise does it ask `holds_for`.

### Faults

Compiled code never raises a fault. Every check that can fail leaves the block before its instruction: a load or store outside memory or outside an allocated heap block, a push or pop beyond the stack, `ENTER` or `PUSHM` without room, a division by zero or of the lowest integer by -1, and `MEMCPY` or `MEMSET` that would fault. `cpu_run` then runs the instruction itself, finds the same fault and stops, and `vm_step` reports the fault as it would without compiled code.

### Memory

Loads and stores make the checks of `cpu_run` inline. An address based on SP or BP is first compared with the stack, which needs nothing more. Otherwise the end of the access is compared with the size of memory and with the bounds of the heap. An access inside the heap has the rights of its first and last 8 bytes looked up in a stub, as `memory_heap_allows` does, and anything else asks `cpu_reach`, the check that `cpu_run` falls back on too.

### Code that changes

A store, `MEMCPY` or `MEMSET` below the end of the decoded code calls `cpu_forget`. When the bytes lie in a word that compiled code came from, or in the word after one, which an instruction there can take as its extension, `jit_written` drops all compiled code, which gets compiled again once jumps go there often enough, and the block leaves after the instruction. Otherwise the block goes on. A table of the words that blocks came from keeps writes next to compiled code, such as to the stack of a program that runs below its kernel under MiniDos, from dropping it.

## Leaving and linking

Compiled code returns to `cpu_run`, at its `D_JIT` case, in one of three ways:

| | |
|---|---|
| `JIT_NEXT` | Go on at a word without compiled code. `cpu_run` counts the jump to it |
| `JIT_HERE` | Run the instruction at the word in `cpu_run`: one that would fault, one the budget does not cover, or a syscall in user mode |
| `JIT_LEAVE` | Go on at an address outside the decoded code, which `vm_step` deals with |

Between blocks, compiled code does not return at all. A jump to a word with compiled code is a direct jump to that code, and a block that jumps to its own start jumps to its own entry. A jump to a word without compiled code leaves with `JIT_NEXT` and is noted for that word: when the word gets compiled, the jump is patched to go there. A jump to a computed address, as `RET`, `JMP R5` and `CALL [table]` make, looks up the word in the table of compiled code and leaves when there is none.

`CALL` pushes the return address and notes the call in the [shadow call stack](../README.md#running-programs) for backtraces, both inline. `RET` drops the calls whose return address now lies below SP through a helper, as `cpu_run` does.

## Dropping compiled code

Compiled code is only ever dropped as a whole: when a program writes to bytes that code was compiled from, when the 32 MB are full, in which case the block that did not fit is compiled again into the empty memory, and when the VM's memory is reset. The words that compiled code started at are decoded again when `cpu_run` next reaches them, start counting jumps from scratch, and the jumps that waited for other words are forgotten.

## Helpers

Compiled code calls C for what is rare or long:

| Helper | For |
|---|---|
| `holds_for` | A condition whose flags the compiler cannot see |
| `partial_for` | The flags after `SHL`, `MUL`, `INC` and the like, when it is not known what set the flags before |
| `binary_for` | `ADDC`, `SUBC`, `MULH`, `UMULH`, `ROL`, `ROR`, and arithmetic and logic with an operand in memory |
| `cpu_reach`, `cpu_push_slot`, `cpu_pop_slot` | Accesses that the inline checks of memory and stack leave open |
| `written_for` | Stores below the end of the decoded code, through `cpu_forget` |
| `fill_for` | `MEMCPY` and `MEMSET`, through `cpu_fill`, the code that `cpu_run` runs them with |
| `syscall_for` | The memory syscalls 20 to 29 in supervisor mode, which `cpu_run` runs itself |
| `push_frame_for`, `pop_frames_for` | A shadow call stack that is full, and `RET` |

## Testing

- `tests/programs/jit.asm` and the other `jit_*` programs run with `-j 1`. They cover flags that one block sets and another reads, calls with frames, stack instructions, a loop that rewrites itself and an instruction limit inside a compiled loop, and the edges that random programs miss: the lowest integer divided by -1, pushes at the top of the stack, and loads that reach from below the heap into it.
- `VM_FLAGS='-j 1' make test` runs the whole suite with code compiled from the first jump to it, and `VM_FLAGS='-j 0' make test` without any compiled code.

## Source

| File | |
|---|---|
| `src/core/jit.c` | The encoder, the entry and exit code, the helpers, the compiler and the code cache |
| `src/core/jit.h` | What `cpu_run` calls |
| `src/core/run.c` | `cpu_run`: counting jumps, entering compiled code and running what it hands back |
| `src/core/run.h` | The decoded instructions and the flags that both share, and the checks of `cpu_run` that compiled code calls |

In `jit.c`, `jit_compile` and `compile_block` drive the compiler, `compile_item` writes the code of one instruction, `compile_stub` the slow paths and `write_entry` the entry and exit code.

## Limits

- Only x86-64 Linux and macOS, built with GCC or Clang. Elsewhere `vm` interprets every instruction.
- Only SP and R0 live in host registers. The other VM registers are loaded and stored around every instruction that uses them.
- An instruction that sets flags stores them whenever an instruction that can leave the block follows before the next one that sets them, and most loads and stores can.
- Any write to compiled code drops all of it, so code that keeps rewriting itself is compiled over and over.
