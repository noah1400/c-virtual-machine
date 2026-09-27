# VM32

A 32-bit virtual machine written in C, with its own assembler, linker, disassembler, interactive debugger and a compiler for Ore, a small systems language.

- **`vm`** runs VM32 programs and can trace, disassemble or debug them.
- **`vmasm`** turns assembly source into VM32 binaries or object files. It supports expressions, local labels, macros, structures, conditional assembly, includes and listings.
- **`vmld`** links object files into one program.
- **`vmc0`** compiles [Ore](docs/language.md) programs into VM32 binaries. It is written in C, to compile the Ore compiler written in Ore.

## Contents

- [Building](#building)
- [Quick start](#quick-start)
- [Running programs](#running-programs)
- [The machine](#the-machine)
- [Instruction set](#instruction-set)
- [Assembly language](#assembly-language)
- [Linking](#linking)
- [Ore](#ore)
- [Syscalls](#syscalls)
- [Debugger](#debugger)
- [Binary format](#binary-format)
- [Tests](#tests)
- [Source layout](#source-layout)

## Building

You need a C11 compiler and a POSIX system.

```sh
make          # builds ./vm, ./vmasm, ./vmld and ./vmc0
make test     # builds them and runs the test suite
make clean
```

You can override `CC`, `CFLAGS` and `LDFLAGS`. For example, for a sanitizer build:

```sh
make clean
make CC=clang CFLAGS="-O1 -g -fsanitize=address,undefined" LDFLAGS="-fsanitize=address,undefined"
```

## Quick start

Save this as `hello.asm`:

```asm
; Prints a greeting and the sum 1 + 2 + ... + 10
.text
main:
    LOAD R0, greeting
    SYSCALL #2              ; print string at R0

    LOAD R8, #0             ; sum
    LOAD R9, #10            ; counter
.loop:
    ADD R8, R9
    LOOP R9, .loop          ; decrement R9, jump while not zero

    MOVE R0, R8
    SYSCALL #1              ; print integer
    LOAD R0, #'\n'
    SYSCALL #0              ; print character
    HALT

.data
greeting:
    .asciiz "Hello from VM32! Sum: "
```

Then assemble and run it:

```console
$ ./vmasm hello.asm -o hello.bin
$ ./vm hello.bin
Hello from VM32! Sum: 55
```

`assembler/examples` has more programs. They range from a Fibonacci printer to `MiniDos`, a small command shell with a file system on the [disk](#disk), which you assemble from `MiniDos/main.asm`. `kernel.asm` runs a user program under paging, `mandelbrot.asm` draws with floats, `snake.asm` is a game for the [display](#display) and [keyboard](#keyboard), and `cat.asm` prints the files named on its command line:

```console
$ ./vmasm assembler/examples/cat.asm -o cat.bin
$ ./vm cat.bin hello.asm
```

## Running programs

```
./vm [options] program.bin [arguments...]
```

Everything after the program path is passed to the program, which reads it with [syscall 34](#process-and-random-numbers).

| Option | Effect |
|---|---|
| `-b FILE` | Attach FILE as the [disk](#disk) image |
| `-c FILE` | Write how often each source line ran to FILE, or to stdout for `-` |
| `-d` | Start the interactive [debugger](#debugger) |
| `-D` | Disassemble the program instead of running it |
| `-H COUNT` | When the program stops with an error, first show the last COUNT instructions and the registers they changed |
| `-k FILE` | Take [keyboard](#keyboard) input from a key script |
| `-L SPEC` | Print values each time execution reaches a location (see below). Can be given up to 32 times |
| `-m KB` | Memory size in KB, 128 to 1048576 (default 1024) |
| `-n COUNT` | Stop with an error after COUNT instructions |
| `-p` | Print an execution profile on stderr when the program stops |
| `-s` | Print [display](#display) frames as plain text instead of drawing them |
| `-S KB` | Stack size in KB, at least 4 and less than the memory (default 64) |
| `-t` | Print each instruction on stderr before it executes |
| `-v` | Print loading and execution statistics on stderr |
| `-x FILE` | Start the debugger and run its commands from FILE |
| `-h` | Show help |

The exit status is:

- 0 after `HALT`.
- The low 8 bits of the code passed to [syscall 30](#process-and-random-numbers).
- 1 when the program faults or cannot be loaded.
- 128 plus the signal number when SIGINT, SIGTERM or SIGHUP stops it.

Outside the debugger, a signal stops the machine before its next instruction, so the report shows where the program was and the devices restore the terminal. A second signal ends `vm` at once.

A fault stops the machine. The report names the faulting instruction, and its source line too when the binary carries debug information:

```console
$ ./vm fault.bin
vm: error: Division by zero
vm: at 0x0004 fault.asm:5: DIV R8, #0x0
```

Below that, the report lists the calls and interrupts that led to the fault, innermost first, and folds repeats of the same call:

```console
vm: at 0x0014 <divide+4> fault.asm:16: DIV R8, R9
vm: #1 0x0008 <outer> fault.asm:11: CALL divide
vm: #2 0x0000 <main> fault.asm:7: CALL outer
```

The VM keeps a shadow stack of the calls and interrupts in flight for this. Frames that the program left without returning, for example by switching stacks, are left out.

`-H COUNT` puts the last COUNT instructions before the report, with the registers each one changed:

```console
$ ./vm -H 3 fault.bin
vm: the last 3 instructions:
0x0004 <main+4>             LOAD R9, #0x3            R9=0x00000003
0x0008 <main+8>             ADD R8, R9               R8=0x00000005
0x000C <main+12>            LOAD R10, #0x0
vm: error: Division by zero
vm: at 0x0010 <main+16> fault.asm:12: DIV R8, R10
```

A trace shows the address, the nearest label and the instruction:

```console
$ ./vm -t hello.bin 2>&1 >/dev/null | head -4
0x0000 <main>               LOAD R0, #0x1000  ; greeting
0x0004 <main+4>             SYSCALL #2
0x0008 <main+8>             LOAD R8, #0x0
0x000C <main+12>            LOAD R9, #0xA
```

A profile adds up the executed instructions under the closest preceding label:

```console
$ ./vm -p hello.bin
Hello from VM32! Sum: 55
vm: profile of 29 instructions
       count   share  location
          25   86.2%  0x0010 <main.loop>
           4   13.8%  0x0000 <main>
```

A coverage listing shows every source line that produced code with the number of times it ran, and marks the lines that never ran with `#####`. A line that became several instructions, such as a macro call or a line named by [`.loc`](#directives), ran as often as its instruction that ran most. Each file starts with a summary, and a total follows when there are several files:

```console
$ ./vm -c - count.bin
count.asm: 6 of 7 lines ran
        1:    6: LOAD R8, #3
        3:    8: DEC R8
        3:    9: JNZ count
        1:   10: CMP R8, #0
        1:   11: JZ done
    #####:   12: LOAD R8, #1
        1:   14: HALT
```

The exit status is 1 when the listing cannot be written.

A logpoint prints registers and memory on stderr each time execution reaches its location, without stopping the program. It is written as `LOCATION:ITEM,ITEM...`, where the location is a label or an address and each item is one of these:

- A register, such as `R6` or `SP`.
- `[ADDRESS]`, the memory at an address that adds up at most one register, numbers and symbols, as in `[count]`, `[R8+24]` or `[buffer+R5]`.

An item can end in `:` and a format: `x` for hex (the default), `d` for signed and `u` for unsigned decimal, `c` for a character, `s` for a string and `f` for a float. For memory, `b` or `w` reads a byte or a 16-bit word instead of 32 bits. With `s`, a register is taken as the address of the string, while `[ADDRESS]` names the string itself:

```console
$ ./vm -L 'find_file:R6:s,[R7+24]:d' minidos.bin
log 0x0674 <find_file> R6="a.txt" [R7+24]=0
```

`-D` prints the header, then the code with its labels, then a hex dump of the data. A file without a VM32 header is treated as raw code and loaded at address 0.

## The machine

### Registers

There are sixteen 32-bit registers. The special registers also have aliases.

| Register | Aliases | Use |
|---|---|---|
| R0 | ACC, R0_ACC | Accumulator; syscall argument and result |
| R1 | BP, R1_BP | Base pointer for stack frames |
| R2 | SP, R2_SP | Stack pointer |
| R3 | PC, R3_PC | Program counter. While an instruction executes, it already holds the address of the next one |
| R4 | SR, R4_SR | Status register (flags) |
| R5–R7 | | General purpose; syscall arguments. R5 receives the syscall status |
| R8–R14 | | General purpose |
| R15 | LR, R15_LR | General purpose; link register by convention |

Status register bits:

| Bit | Flag | Meaning |
|---|---|---|
| 0x01 | Z | The result was zero |
| 0x02 | N | Bit 31 of the result was set |
| 0x04 | C | Unsigned carry out of an addition, or borrow in a subtraction |
| 0x08 | O | Signed overflow |
| 0x10 | I | Device interrupts are enabled |
| 0x20 | D | Reserved |
| 0x40 | S | Supervisor mode; clear in [user mode](#user-mode) |
| 0x80 | T | Trap after every instruction |

### Control registers

`MFCR Rd, NAME` reads a control register and `MTCR NAME, Rs` writes one. A register can also be given by number, as in `MFCR R8, #3`. Both instructions are privileged.

| # | Name | Meaning | Initial value |
|---|---|---|---|
| 0 | IVTB | Address of the [interrupt vector table](#interrupts-and-exceptions); 0 means there is none | 0 |
| 1 | KSP | Stack pointer for interrupts that arrive in user mode; 0 keeps the current SP | 0 |
| 2 | PTB | Physical address of the page directory; 0 turns [paging](#paging) off | 0 |
| 3 | FADDR | Address that caused the last memory fault | 0 |
| 4 | ECODE | Details of the last page fault | 0 |
| 5 | SLO | Lowest address the stack may grow to | Memory size − the stack size |
| 6 | SHI | Highest stack address, where SP and BP start | Memory size |
| 7 | HEAPLO | Start of the heap | End of the program, rounded up to 16 |
| 8 | HEAPHI | End of the heap | SLO |

`RESET` restores these initial values along with the registers.

### Memory

Memory is flat, byte-addressed and little-endian. It is 1 MB by default, and `-m` sets any size from 128 KB to 1 GB. The stack takes 64 KB of it unless `-S` gives it more or less. Unaligned data accesses are allowed.

| Region | Contents |
|---|---|
| From 0 | Code, from `.text` |
| From the next page boundary | Data, from `.data` |
| From the end of the data up to SLO | Heap |
| The top 64 KB, or what `-S` sets | Stack, growing down from the top of memory |

Every access is checked:

- It must lie inside physical memory, after translation when paging is on.
- An access that touches the heap must stay inside one allocated block and respect that block's protection.
- Pushing below SLO faults with stack overflow, and popping above SHI with stack underflow.
- Any stack operation faults while SP lies outside SLO to SHI.

Setting SLO to 0 and SHI to 0xFFFFFFFF turns the stack checks off.

### Heap

The heap is a first-fit allocator. Its bookkeeping lives outside VM memory, where programs cannot damage it.

- Sizes are rounded up to a multiple of 8, with a minimum of 8 bytes.
- Every block is preceded by an 8-byte guard gap. Touching it faults, which catches small overruns.
- New blocks are zero-filled.
- Freeing a block twice, or freeing an address that is not the start of a block, is an error.
- Writing HEAPLO or HEAPHI empties the heap. Setting them equal turns the heap off, which leaves that memory to the program.

`PROTECT` sets a block's permissions: 1 read, 2 write, 4 execute. New blocks get all three. Code only runs from the heap if its block has execute permission.

### Interrupts and exceptions

The vector table at IVTB holds one 32-bit handler address for each of the 256 vectors. The entry for vector *v* is at `IVTB + 4*v`. An entry of 0, or an IVTB of 0, means there is no handler.

When the CPU enters a handler, it:

1. Switches to supervisor mode. If it was in user mode and KSP is set, it also switches to the stack at KSP.
2. Pushes R15 down to R0, so `[SP + 4*n]` holds R*n*. The saved SP and SR are the interrupted values, and the saved PC is where execution resumes.
3. Clears I and T.
4. Jumps to the handler.

`IRET` restores all sixteen registers from that frame, SP included, so it can return to user mode and to a user stack. A handler can read or change the interrupted registers through the frame.

Handlers are entered for four reasons:

- **Software interrupts:** `INT #v` raises vector *v* at once, whether or not I is set.
- **Device interrupts:** these are latched, then delivered before the next instruction once I is set (`STI`).
- **Exceptions:** a fault with a code from 1 to 14 goes to the vector with the same number (see [error codes](#error-codes)). The registers are put back as they were before the faulting instruction, and the saved PC points at that instruction. A handler can therefore fix the cause and return to retry it, or skip it. The instruction is 8 bytes long when bit 23 of its first word is set, and 4 bytes otherwise. Memory faults leave the address in FADDR.
- **Single-step trap:** while T is set, vector 15 is raised after every instruction.

Without a handler, a fault stops the machine. So does a fault while a handler is being entered, such as a stack overflow while the frame is pushed; that is reported as a double fault.

### User mode

The CPU starts in supervisor mode, with S set. A kernel enters user mode by executing `IRET` with a frame whose saved SR has S clear. In user mode:

- The privileged instructions `HALT`, `CLI`, `STI`, `IRET`, `IN`, `OUT`, `RESET`, `MFCR`, `MTCR`, `SYSCALL` and `PROTECT` fault with a privilege violation.
- Instructions cannot change S, I or T. Writes to SR keep their old values.
- Pages whose entry lacks the user bit cannot be touched.

User programs reach the kernel through `INT`. `assembler/examples/kernel.asm` shows the whole arrangement.

### Paging

When PTB is not 0, virtual addresses are translated through two levels of tables with 4 KB pages:

- Bits 31–22 of an address select an entry in the page directory at PTB.
- Bits 21–12 select an entry in the page table that directory entry points to.
- Bits 11–0 are the offset in the page.

An entry holds the physical address of a page table or page in bits 31–12, and flags in its low bits. Directory entries only use P.

| Bit | Flag | Meaning |
|---|---|---|
| 0x1 | P | Present |
| 0x2 | W | Writable |
| 0x4 | U | Accessible in user mode |
| 0x8 | X | Executable |

A missing entry or a forbidden access raises a page fault, code 14. FADDR holds the virtual address, and ECODE describes the access:

| Bit | Meaning |
|---|---|
| 1 | The page is present but does not allow the access |
| 2 | The access was a write |
| 4 | The CPU was in user mode |
| 8 | The access was an instruction fetch |

Syscall buffers and heap blocks are virtual addresses too. The vector table is read with supervisor rights, even when user code is interrupted.

### I/O ports

`IN Rd, port` and `OUT port, Rs` take the port as an immediate or a register. Both are privileged. Accessing a port that has no device faults.

| Port | Device | IN | OUT |
|---|---|---|---|
| 0x00 | Console | Next byte from stdin, or 0 at end of input | Writes the low byte to stdout |
| 0x01 | Console | 0 | Writes the low byte to stderr |
| 0x40 | Timer interval | Current interval | Sets the number of instructions between timer interrupts; 0 stops the timer |
| 0x41 | Timer vector | Current vector | Sets the vector raised on expiry (0–255) |
| 0x42 | Timer ticks | Expirations so far | Sets the counter |
| 0x50 | Display buffer | Buffer address | Sets the address of the character buffer and clears the terminal; 0 turns the display off |
| 0x51 | Display refresh | 0 | Draws the cells that changed since the last refresh |
| 0x52 | Display columns | 80 | Ignored |
| 0x53 | Display rows | 25 | Ignored |
| 0x60 | Keyboard status | Bit 0: a key is waiting. Bit 1: the input has ended | Ignored |
| 0x61 | Keyboard data | The next key, or 0 if none is waiting | Ignored |
| 0x62 | Keyboard vector | Current vector | Sets the vector requested while keys wait; 0 turns it off |
| 0x70 | Disk sector | First sector | Sets the first sector of the next transfer |
| 0x71 | Disk buffer | Buffer address | Sets the address of the transfer buffer |
| 0x72 | Disk count | Sector count | Sets the number of sectors per transfer (default 1) |
| 0x73 | Disk command | Result of the last command | 1 reads sectors into memory, 2 writes memory to sectors |
| 0x74 | Disk size | Sectors on the disk, 0 without an image | Ignored |

`assembler/examples/timer.asm` drives a main loop from timer interrupts. The display and the disk take physical addresses, which do not go through [paging](#paging).

#### Display

The display shows an 80×25 buffer of cells in memory. A cell is two bytes: the character, then an attribute with the foreground color in the low nibble and the background color in the high nibble. Colors 0 to 7 are black, red, green, yellow, blue, magenta, cyan and white, and 8 to 15 are their bright versions. Characters outside printable ASCII show as spaces.

Setting the buffer address clears the terminal and hides the cursor. Each write to the refresh port then draws the cells that changed with ANSI escape sequences, so a program updates its buffer and refreshes once per frame. When the display is turned off or the program ends, the cursor reappears below the display.

With `vm -s`, a refresh prints the characters as plain text instead, when they changed since the last frame. A header line gives the number of the refresh and the instructions executed so far, and trailing spaces are left out:

```console
$ ./vm -s snake.bin 0 < keys.txt | tail -26 | head -4
--- refresh 31, instruction 45700 ---
 SNAKE   arrows or WASD steer, q quits                              score:    1
################################################################################
#                                                                              #
```

#### Keyboard

The keyboard reads stdin directly. The first access to one of its ports switches a terminal to unbuffered input without echo, and `vm` restores the terminal when the program ends. Ctrl-C still stops the machine. Keys are the bytes typed, except for the arrow keys:

| Key | Code |
|---|---|
| Up | 0x100 |
| Down | 0x101 |
| Right | 0x102 |
| Left | 0x103 |

Other escape sequences are dropped, and up to 64 keys wait in a queue. Reading the status or the data port checks for new input. While the vector port holds a vector, the keyboard also checks every 10000 instructions and requests the interrupt for as long as keys wait.

When stdin is a file or a pipe, its bytes arrive as keys and the status reports the end of input. Reading the console as well can split the input, because the console reads ahead into a buffer.

`vm -k FILE` takes the keys from a key script instead, which makes runs of interactive programs repeatable. Each line holds an instruction count, or `+N` for N instructions after the line before, then a space and the keys. `\n`, `\r`, `\t`, `\e`, `\\` and `\xNN` stand for single bytes, and lines that are empty or start with `#` are skipped. The keys become available once the VM has executed that many instructions, and the input ends after the last line:

```
# Up arrow at instruction 50000, then q 20000 instructions later
50000 \e[A
+20000 q
```

With a script, the keyboard leaves the terminal and stdin alone, and looks for keys after every instruction, so an interrupt comes at the exact count.

#### Disk

`vm -b FILE` attaches a disk image of 512-byte sectors; a partial sector at the end of FILE is left out. Writing a command to port 0x73 moves the given number of sectors, starting at the first sector, between the image and the buffer. The transfer is done when `OUT` returns, and reading the port gives the result:

| Result | Meaning |
|---|---|
| 0 | Done |
| 1 | No disk image is attached |
| 2 | The sectors reach past the end of the disk |
| 3 | The buffer reaches past the end of memory |
| 4 | Unknown command |
| 5 | The image is read-only |
| 6 | The host reported an error |

An image that `vm` may not write is attached read-only. MiniDOS keeps up to 32 files of 4 KB on a disk of at least 259 sectors:

```console
$ dd if=/dev/zero of=disk.img bs=512 count=300
$ ./vmasm assembler/examples/MiniDos/main.asm -o minidos.bin
$ ./vm -b disk.img minidos.bin
```

Its `format` command creates an empty file system, and `dir`, `type`, `write`, `append` and `del` work with the files.

## Instruction set

### Encoding

Each instruction is one or two little-endian 32-bit words at a 4-byte aligned address. Jumping to an unaligned address faults.

```
 31        24 23    20 19    16 15    12 11                0
+------------+--------+--------+--------+-------------------+
|   opcode   |  mode  |   r1   |   r2   |     immediate     |
+------------+--------+--------+--------+-------------------+
```

In the IMM, MEM, STK and BAS modes, the r2 field holds the upper 4 bits of a 16-bit immediate. When bit 3 of the mode is set, a second word follows and holds the whole 32-bit immediate or displacement. The assembler adds that extension word whenever a value does not fit the first word. The top three bits of the opcode select the instruction group.

### Addressing modes

| Mode | Syntax | Operand | Range in the first word |
|---|---|---|---|
| IMM | `#expr` or `expr` | The value itself, sign-extended | −32768 to 32767 |
| REG | `R5` | The register | |
| MEM | `[expr]` | Memory at an absolute address | 0 to 0xFFFF |
| REGM | `[R5]` | Memory at the address in a register | |
| IDX | `[R5+expr]`, `[R5-expr]` | Register plus displacement | −2048 to 2047 |
| STK | `[SP]`, `[SP+expr]` | SP plus displacement | −32768 to 32767 |
| BAS | `[BP]`, `[BP-expr]` | BP plus displacement | −32768 to 32767 |

Values outside these ranges go in an extension word, which holds any 32-bit value.

A bare label is an immediate. `LOAD R0, msg` loads the address of `msg`, while `LOAD R0, [msg]` loads the word stored there.

The tables below use three operand kinds:

- *src* accepts every mode.
- *addr* accepts only the memory modes.
- *val* accepts IMM or REG.

### Data transfer

| Instruction | Opcode | Operation |
|---|---|---|
| `NOP` | 0x00 | Does nothing |
| `LOAD Rd, src` | 0x01 | Rd = src (memory operands are read as 32 bits) |
| `STORE Rs, addr` | 0x02 | Stores Rs |
| `MOVE Rd, Rs` | 0x03 | Rd = Rs |
| `LOADB Rd, src` | 0x04 | Rd = src, zero-extended from 8 bits |
| `STOREB Rs, addr` | 0x05 | Stores the low byte of Rs |
| `LOADW Rd, src` | 0x06 | Rd = src, zero-extended from 16 bits |
| `STOREW Rs, addr` | 0x07 | Stores the low 16 bits of Rs |
| `LEA Rd, addr` | 0x08 | Rd = the effective address |

`LOAD Rd, #value` accepts any 32-bit constant, including negative numbers and [floats](#floating-point).

### Arithmetic

| Instruction | Opcode | Operation | Flags |
|---|---|---|---|
| `ADD Rd, src` | 0x20 | Rd += src | Z N C O |
| `SUB Rd, src` | 0x21 | Rd −= src | Z N C O |
| `MUL Rd, src` | 0x22 | Rd *= src, keeping the low 32 bits. O is set when the unsigned product needed more | Z N O |
| `DIV Rd, src` | 0x23 | Unsigned division | Z N |
| `MOD Rd, src` | 0x24 | Unsigned remainder | Z N |
| `INC Rd` | 0x25 | Rd += 1 | Z N O |
| `DEC Rd` | 0x26 | Rd −= 1 | Z N O |
| `NEG Rd` | 0x27 | Rd = −Rd | Z N O |
| `CMP Rd, src` | 0x28 | Sets the flags of Rd − src | Z N C O |
| `ADDC Rd, src` | 0x2A | Rd += src + C | Z N C O |
| `SUBC Rd, src` | 0x2B | Rd −= src + C | Z N C O |
| `IDIV Rd, src` | 0x2C | Signed division, rounding toward zero | Z N |
| `IMOD Rd, src` | 0x2D | Signed remainder, with the sign of the dividend | Z N |
| `MULH Rd, src` | 0x2E | Rd = the upper 32 bits of the signed 64-bit product | Z N |
| `UMULH Rd, src` | 0x2F | Rd = the upper 32 bits of the unsigned 64-bit product | Z N |

Division by zero faults. So does signed division of −2147483648 by −1.

### Floating point

Floats are IEEE single-precision values kept in the general registers. The assembler writes them as literals such as `#1.5` or `#-2e-3`, and [syscall 7](#console) prints them.

| Instruction | Opcode | Operation | Flags |
|---|---|---|---|
| `FADD Rd, src` | 0x30 | Rd += src | Z N; clears C and O |
| `FSUB Rd, src` | 0x31 | Rd −= src | Z N; clears C and O |
| `FMUL Rd, src` | 0x32 | Rd *= src | Z N; clears C and O |
| `FDIV Rd, src` | 0x33 | Rd /= src. Dividing by zero gives an infinity or NaN | Z N; clears C and O |
| `FCMP Rd, src` | 0x34 | Compares Rd with src | Z C O; clears N |
| `FSQRT Rd, src` | 0x35 | Rd = the square root of src | Z N; clears C and O |
| `FNEG Rd` | 0x36 | Flips the sign of Rd | Z N; clears C and O |
| `FABS Rd` | 0x37 | Clears the sign of Rd | Z N; clears C and O |
| `ITOF Rd, src` | 0x38 | Rd = the signed integer src as a float | Z N; clears C and O |
| `FTOI Rd, src` | 0x39 | Rd = src truncated to a signed integer | Z N O; clears C |

- **Comparing:** `FCMP` sets Z when the values are equal and C when Rd is smaller, so the unsigned jumps `JB`, `JBE`, `JA` and `JAE` apply. If either value is NaN, the comparison is unordered and sets Z, C and O.
- **Converting:** `FTOI` gives −2147483648 and sets O for NaN and for values outside the 32-bit range.

### Logic and shifts

| Instruction | Opcode | Operation | Flags |
|---|---|---|---|
| `AND Rd, src` | 0x40 | Rd &= src | Z N; clears C and O |
| `OR Rd, src` | 0x41 | Rd \|= src | Z N; clears C and O |
| `XOR Rd, src` | 0x42 | Rd ^= src | Z N; clears C and O |
| `NOT Rd` | 0x43 | Rd = ~Rd | Z N |
| `SHL Rd, src` | 0x44 | Shift left | Z N C |
| `SHR Rd, src` | 0x45 | Logical shift right | Z N C |
| `SAR Rd, src` | 0x46 | Arithmetic shift right | Z N C |
| `ROL Rd, src` | 0x47 | Rotate left | Z N C |
| `ROR Rd, src` | 0x48 | Rotate right | Z N C |
| `TEST Rd, src` | 0x49 | Sets the flags of Rd & src | Z N; clears C and O |
| `POPCNT Rd, src` | 0x4A | Rd = the number of set bits in src | Z N; clears C and O |
| `CLZ Rd, src` | 0x4B | Rd = the number of leading zero bits in src, 32 for 0 | Z N; clears C and O |
| `CTZ Rd, src` | 0x4C | Rd = the number of trailing zero bits in src, 32 for 0 | Z N; clears C and O |
| `BSWAP Rd` | 0x4D | Reverses the byte order of Rd | Z N |
| `SETcc Rd` | 0x4E | Rd = 1 if condition *cc* holds, otherwise 0 | None |

`SETcc` takes every condition of the [conditional jumps](#control-flow), under all their names: `SETZ`, `SETNE`, `SETL`, `SETAE` and so on.

Shifts and rotates use the low 5 bits of the operand as the count.

- After a shift, C holds the last bit shifted out.
- After a rotate, C holds the bit that wrapped around.
- A count of 0 leaves C unchanged.

### Control flow

| Instruction | Opcode | Operation |
|---|---|---|
| `JMP target` | 0x60 | Jumps |
| `Jcc target` | 0x61–0x68, 0x70–0x74 | Jumps if the condition holds (see the table below) |
| `CALL target` | 0x6A | Pushes the return address and jumps |
| `RET [#n]` | 0x6B | Pops the return address, then drops *n* bytes of arguments |
| `SYSCALL #n` | 0x6C | Performs [system call](#syscalls) *n* |
| `LOOP Rc, target` | 0x6F | Rc −= 1, then jumps if Rc ≠ 0. Flags are not changed |

A *target* can be:

- A label or address, as in `JMP loop`. It is stored as an offset from the next instruction, so the code runs at any address.
- A register, as in `JMP R8`.
- A memory operand that holds the address, as in `JMP [table+8]`.

| Jump | Opcode | Also written | Taken when |
|---|---|---|---|
| `JZ` | 0x61 | `JE` | Z |
| `JNZ` | 0x62 | `JNE` | not Z |
| `JN` | 0x63 | | N |
| `JP` | 0x64 | | not N and not Z |
| `JO` | 0x65 | | O |
| `JC` | 0x66 | `JB`, `JNAE` | C (unsigned below) |
| `JBE` | 0x67 | `JNA` | C or Z |
| `JA` | 0x68 | `JNBE` | not C and not Z |
| `JL` | 0x70 | `JNGE` | N ≠ O (signed less) |
| `JGE` | 0x71 | `JNL` | N = O |
| `JLE` | 0x72 | `JNG` | Z, or N ≠ O |
| `JG` | 0x73 | `JNLE` | not Z and N = O |
| `JAE` | 0x74 | `JNB`, `JNC` | not C |

### Stack

| Instruction | Opcode | Operation |
|---|---|---|
| `PUSH src` | 0x80 | SP −= 4, then stores src at SP |
| `POP Rd` | 0x81 | Loads Rd from SP, then SP += 4 |
| `PUSHF` | 0x82 | Pushes SR |
| `POPF` | 0x83 | Pops SR |
| `PUSHA` | 0x84 | Pushes R15 down to R0, so `[SP + 4*n]` holds R*n*. The saved SP is its value before `PUSHA` |
| `POPA` | 0x85 | Restores the registers saved by `PUSHA`, except SP and PC |
| `ENTER #n` | 0x86 | Pushes BP, sets BP = SP, then reserves *n* bytes |
| `LEAVE` | 0x87 | Sets SP = BP, then pops BP |

After `CALL` and `ENTER`, `[BP+4]` is the return address and `[BP+8]` is the last argument pushed before the call.

### System

| Instruction | Opcode | Operation |
|---|---|---|
| `HALT` | 0xA0 | Stops the program |
| `INT #v` | 0xA1 | Raises interrupt *v* (0–255) |
| `CLI` | 0xA2 | Clears I |
| `STI` | 0xA3 | Sets I |
| `IRET` | 0xA4 | Returns from an interrupt handler |
| `IN Rd, val` | 0xA5 | Reads a port |
| `OUT val, Rs` | 0xA6 | Writes Rs to a port |
| `CPUID` | 0xA7 | Reports machine information, selected by R0 (see the table below) |
| `RESET` | 0xA8 | Restores the initial registers and control registers, empties the heap and jumps to the entry point. Memory is kept |
| `DEBUG` | 0xA9 | A breakpoint under `vm -d`. Does nothing otherwise |
| `MFCR Rd, NAME` | 0xAA | Reads a [control register](#control-registers) |
| `MTCR NAME, Rs` | 0xAB | Writes a control register |

`HALT`, `CLI`, `STI`, `IRET`, `IN`, `OUT`, `RESET`, `MFCR` and `MTCR` are privileged, as are `SYSCALL` and `PROTECT`.

| R0 | `CPUID` result |
|---|---|
| 0 | R0 = highest function (4). R5 and R6 = the vendor string "VM32CPU" |
| 1 | R0 = version, 0x00020000. R5 and R6 = feature bits. In R6, bits 1 to 4 stand for the timer, display, keyboard and disk |
| 2 | R0 = memory size. R5 = page size. R6 = stack size |
| 3 | R0 = number of instructions. R5 = mask of addressing modes. R6 = mask of instruction groups |
| 4 | R0 = instructions executed. R5 = 2 under the debugger, plus 4 while I is set, 8 in supervisor mode and 16 while paging is on. R6 = the last error code |

### Memory management

| Instruction | Opcode | Operation |
|---|---|---|
| `ALLOC Rd, val` | 0xC0 | Rd = the address of a new zero-filled block of *val* bytes. Faults if the heap is exhausted |
| `FREE Rs` | 0xC1 | Frees the block at Rs |
| `MEMCPY Rd, Rs, size` | 0xC2 | Copies *size* bytes from Rs to Rd. The ranges may overlap |
| `MEMSET Rd, Rv, size` | 0xC3 | Fills *size* bytes at Rd with the low byte of Rv |
| `PROTECT Ra, val` | 0xC4 | Sets the permissions of the block at Ra |

*size* is an immediate or a register. Syscalls 20 to 22 do the same jobs, but they report failures in R5 instead of faulting.

## Assembly language

```
label:  MNEMONIC operand, operand   ; comment
```

- Each line holds at most one statement, which can follow a label. Comments start with `;`.
- Mnemonics, registers and directives are case-insensitive. Symbols are case-sensitive.
- A label that starts with a dot is local to the global label before it. For example, `.loop` after `main:` defines `main.loop`, and other code can reach it under that full name.
- Instructions go in `.text`, which starts at 0x0000. Data goes in `.data`, which starts on the first page boundary after the code. You can switch between the two sections as often as you like.
- An instruction gets an extension word when an operand needs one. For operands that refer to later symbols, the assembler repeats its layout pass until no address moves.

### Expressions

Operands and directive arguments are integer expressions. An expression can contain:

- **Numbers:** `42`, `0x2A`, `0b101010`, `0o52`, or C-style octal such as `052`.
- **Floats:** decimal numbers with a fraction or an exponent, such as `1.5` or `3e8`, stand for the bits of a single-precision float. They can take a sign, but no other operator.
- **Characters:** `'A'`, `'\n'`.
- **Symbols.**
- **`$`:** the address of the current statement.
- **C operators:** these use C precedence, from tightest to loosest:

| Operators |
|---|
| unary `-` `+` `~` `!` |
| `*` `/` `%` |
| `+` `-` |
| `<<` `>>` |
| `<` `<=` `>` `>=` |
| `==` `!=` |
| `&` |
| `^` |
| `\|` |
| `&&` |
| `\|\|` |

Comparisons and logical operators yield 1 or 0. Strings and characters accept the escapes `\n \t \r \0 \a \b \f \v \e \\ \' \"` and `\xHH`.

### Directives

| Directive | Effect |
|---|---|
| `.text`, `.data` | Switch section |
| `.byte v, ...` | 8-bit values. Strings are allowed too |
| `.word v, ...` | 16-bit values |
| `.dword v, ...` | 32-bit values |
| `.float v, ...` | Single-precision floats. Integers are converted |
| `.ascii "s", ...` | String bytes |
| `.asciiz "s", ...` or `.string` | NUL-terminated strings |
| `.space n[, fill]` or `.skip` | *n* bytes of *fill* (default 0) |
| `.align n` | Pad to a multiple of *n*, a power of two up to 4096 |
| `.org offset` | Pad forward to an offset from the start of the current section |
| `.equ NAME, expr` or `.set` | Define a constant. A constant cannot be redefined |
| `.struct NAME` ... `.ends` | Define a [structure](#structures) |
| `.rept count` ... `.endr` | Repeat the lines in between *count* times (see below) |
| `.entry expr` | Start execution here instead of at 0x0000 |
| `.global name, ...` | Export symbols from an object file to the others it is [linked](#linking) with |
| `.extern name, ...` | Use symbols that another object file exports |
| `.error "message"` | Stop assembly with this message. Useful inside `.if` |
| `.include "file"` | Insert a file. It is searched for next to the including file, then in `-I` directories |
| `.incbin "file"[, offset[, length]]` | Insert the bytes of a file, found like an include, or *length* of them from *offset* on |
| `.loc "file", line[, "text"]` | Attribute the code and labels that follow to a line of another source file (see below) |
| `.macro` ... `.endm` | Define a macro |
| `.if`, `.ifdef`, `.ifndef`, `.else`, `.endif` | Conditional assembly |

A value can be written signed or unsigned, but it must fit its width. For example, `.byte` accepts −128 to 255.

The arguments of `.space`, `.align`, `.org` and `.if` decide the layout. They may only use symbols defined earlier, and must not depend on labels whose addresses move when instructions grow.

The count of `.rept` is read along with the source, before any label exists, so it may only use numbers, `-D` constants and `.equ` constants defined earlier from those. Blocks can be nested, and a block with labels in it defines them once per repetition, which clashes; macros with `\@` avoid that.

`.loc` is for compilers that write assembly. Up to the next `.loc`, debug information names the line of the original source instead of the line of assembly, so fault reports, backtraces, the debugger and coverage listings show where the code came from:

```asm
    .loc "fact.c", 3, "return 1 / (n - 1);"
    LOAD R5, #1
    SUB R0, #1
    DIV R5, R0
```

```console
vm: error: Division by zero
vm: at 0x0024 <fact+24> fact.c:3: DIV R5, R0
```

The file name is recorded as written, without reading the file. The text is what coverage listings show, and what the debugger shows when it cannot open the file.

### Structures

Between `.struct NAME` and `.ends`, a label names the offset of a field as `NAME.label`, and the data directives `.byte`, `.word`, `.dword`, `.float`, `.ascii`, `.asciiz`, `.space` and `.align` only reserve room. After `.ends`, `NAME` is the size of the whole structure. Nothing is emitted, and instructions are not allowed inside:

```asm
.struct Entry
name:   .space 24
size:   .dword 0
.ends                           ; Entry.name = 0, Entry.size = 24, Entry = 28

    LOAD R9, [R8 + Entry.size]
entries:
    .space Entry * 32
```

### Macros

```asm
.macro PRINT_NUMBER reg
    CMP \reg, #0
    JNZ .number\@
    LOAD R0, zero_text
    SYSCALL #2
    JMP .done\@
.number\@:
    MOVE R0, \reg
    SYSCALL #1
.done\@:
.endm

    PRINT_NUMBER R8
```

- **Parameters:** separated by commas. In the body, `\name` is replaced by the argument everywhere except inside quotes.
- **Defaults:** `name=text` gives a parameter a default, which replaces an argument that is left out at the end or given empty. `.macro SHOW value, base=10` can be invoked as `SHOW 255` or `SHOW 255, 16`.
- **`\@`:** replaced by a number unique to each expansion, which keeps labels from clashing.
- **Arguments:** split at commas that are not inside quotes, brackets or parentheses.
- **Order:** a macro must be defined before it is used.
- **Nesting:** macros can invoke other macros, up to 32 levels deep, but a macro cannot be defined inside another one.

### Conditional assembly

```asm
.ifdef VERBOSE
    DEBUG
.endif

.if LEVEL >= 2 && MODE == 3
    LOAD R8, #1
.else
    LOAD R8, #0
.endif
```

- `.if` evaluates an expression, which must be known when the line is reached.
- `.ifdef` and `.ifndef` test whether a symbol is defined by an earlier line or on the command line.
- Blocks can be nested.
- `vmasm -D NAME[=VALUE]` defines constants from the command line.

Includes and macro definitions are processed even inside a false block. The included file must therefore exist, and macros defined there are available either way.

### vmasm

```
./vmasm [options] input.asm
```

| Option | Effect |
|---|---|
| `-o FILE` | Output file (default: the input with a `.bin` extension, or `.o` with `-c`) |
| `-c` | Write an object file for [vmld](#linking) instead of a program |
| `-l FILE` | Write a listing: line number, address, encoded bytes and source. Lines from macros are marked with `+` |
| `-I DIR` | Also search DIR for included files |
| `-D NAME[=VALUE]` | Define a constant, 1 unless a value is given |
| `-s` | Print the symbol table |
| `-S` | Leave out debug information |
| `-W` | Warn when a call or syscall overwrites a register value that is read afterwards |
| `-h` | Show help |

`-I` and `-D` also accept their value attached, as in `-DNAME`.

`-W` follows values through the registers. For each routine that is called directly, it works out which registers the routine reads, which it changes, and which of those it returns: the ones that hold a value it computed on every way out, rather than one it only handed to another call, and the ones it reads and then changes. It then follows the code from every label and warns where a register is read after a call or syscall overwrote a value that nothing had read:

```console
$ ./vmasm -W minidos.asm
disk.asm:80: warning: R6 set at line 74 is overwritten by CALL load_directory at line 77 before it is read here
```

A value counts as overwritten when the call changes the register without returning it, or when the program set the register itself and the call returns a result there, as every syscall does with its status in R5. Routines that jump through registers or move SP other than by constants are taken to read and change everything, so they cause no warnings. The check is a heuristic: it can miss mistakes, and it warns about a register that a routine returns on only some paths.

By default, a binary includes the symbols and source lines that the disassembler, the debugger and fault reports use. Errors are reported as `file:line: error: message`, and vmasm then exits with status 1.

## Linking

A program can be split into files that are assembled on their own with `vmasm -c` and linked with `vmld`. A file exports the labels and constants that others may use with `.global`, and declares the ones it uses from others with `.extern`:

```asm
; main.asm                          ; print.asm
.extern print_line                  .global print_line
.text                               .text
main:                               print_line:
    LOAD R0, greeting                   SYSCALL #2
    CALL print_line                     LOAD R0, #'\n'
    HALT                                SYSCALL #0
.data                                   RET
greeting:
    .asciiz "hello"
```

```console
$ ./vmasm -c main.asm
$ ./vmasm -c print.asm
$ ./vmld main.o print.o -o hello.bin
$ ./vm hello.bin
hello
```

In an object file, labels are offsets into their section until the linker places it, so an address can only be stored where the linker can fill it in:

- An instruction operand, which then takes the extension word. Jumps to labels in the same file's code stay as they are.
- A `.dword` value. `.byte` and `.word` cannot hold an address.
- An `.equ` constant, which can in turn be exported.

An address may be added to or subtracted from a number, and two addresses in the same section may be subtracted from each other. Every other use needs a constant, including `.space`, `.align`, `.org` and `.if`. Symbols that a file uses have to be defined in it or declared with `.extern`.

`vmld` places the code of all files from address 0 in the order they are given, and their data from the page after the code, each aligned to the largest `.align` of its section. The program starts at the `.entry` of the one file that has one, at the exported code label given with `-e`, or at address 0. Symbols and source lines of all files end up in the program, so fault reports, backtraces and the debugger work across files.

| Option | Effect |
|---|---|
| `-o FILE` | Output file (default: the first object file with a `.bin` extension) |
| `-e NAME` | Start at the exported code label NAME |
| `-M` | Print where each file's sections and exported symbols went |
| `-S` | Leave out debug information |
| `-h` | Show help |

Linking fails, and names the file, when a symbol is undefined, two files export the same name, or more than one file sets an entry point. `vm` refuses to run an object file.

## Ore

[docs/language.md](docs/language.md) describes the language. `vmc0` compiles a program, meaning its main module and every module that imports, into a binary:

```console
$ ./vmc0 primes.ore
$ ./vm primes.bin
168 primes below 1000, the largest is 997
```

```
Usage: vmc0 [options] program.ore [file.asm...]
  -o FILE   write the binary to FILE (default: the program with .bin)
  -S        write the assembly instead, to FILE or the program with .asm
  -I DIR    look for imported modules in DIR as well
  -L DIR    take the runtime and the standard library from DIR
```

- **How it compiles:** `vmc0` writes the whole program as one assembly file. That file includes `ore/lib/runtime.asm`, any assembly files named on the command line, and the assembly that modules bring along for their `extern fn` functions. `vmc0` then assembles it with the `vmasm` next to it.
- **Standard library:** `import "std/io"` and the other [standard modules](docs/language.md#standard-library) come from `ore/lib/std`.
- **Source lines:** every statement carries a `.loc` line, so fault reports, backtraces, the debugger and coverage listings show Ore source lines.
- **Runtime errors**, such as an index out of bounds, a `null` pointer or a failed `assert`, stop the program with a message, reported at the Ore line that failed. For the `digits.ore` example in the language description:

```console
$ ./vm digits.bin
vm: error: index 10 is out of bounds for length 10
vm: at 0x0040 <digits.digit+16> digits.ore:2: CALL rt.index_error
vm: #1 0x0084 <digits.main+12> digits.ore:6: CALL digits.digit
```

`vmc0` is the bootstrap compiler, so it leaves out `f32`. Its error messages name the file and line of the first problem, and compiling stops there.

## Syscalls

`SYSCALL #n` takes its arguments in R0, R5, R6 and R7 and returns its result in R0. Every syscall also sets R5 to a status: 0 on success, otherwise one of the [error codes](#error-codes).

- Console syscalls fault when given an invalid address.
- File and memory syscalls report problems in R5 instead, and the program keeps running.
- An unknown syscall number faults.

`SYSCALL` is privileged. Under a kernel, user programs ask the kernel through `INT`, and the kernel makes the syscall. Buffer addresses are virtual addresses.

### Console

| # | Name | Arguments | Result |
|---|---|---|---|
| 0 | Print character | R0 = character | |
| 1 | Print integer | R0 = signed integer | |
| 2 | Print string | R0 = address of a NUL-terminated string | |
| 3 | Read character | | R0 = the character. At end of input, R0 = 0 and R5 = 11 |
| 4 | Read line | R0 = buffer, R5 = buffer size | R0 = length. The newline is dropped and a NUL is added. At end of input, R5 = 11 |
| 5 | Print hex | R0 = value | Printed like `0x1f` |
| 6 | Print in base | R0 = unsigned value, R5 = base (2–36) | |
| 7 | Print float | R0 = single-precision float | Printed with up to six significant digits |
| 8 | Clear screen | | |
| 9 | Set color | R0 = foreground + background × 256, each 0–7 | Foreground 0xFF resets the colors. A background of 8 or more selects the terminal's default background |

### Files

| # | Name | Arguments | Result |
|---|---|---|---|
| 10 | Open | R0 = path, R5 = mode: 0 read, 1 write, 2 append, 3 read/write, 4 truncate and read/write | R0 = handle |
| 11 | Close | R0 = handle | |
| 12 | Read | R0 = handle, R5 = buffer, R6 = count | R0 = bytes read; 0 at end of file |
| 13 | Write | R0 = handle, R5 = buffer, R6 = count | R0 = bytes written |
| 14 | Seek | R0 = handle, R5 = signed offset, R6 = origin: 0 start, 1 current, 2 end | R0 = new position |

- Handles 0, 1 and 2 are stdin, stdout and stderr. They work with Read and Write, but cannot be closed or seeked.
- Up to 16 files can be open at once.
- Paths are relative to the directory `vm` runs in.

A program can read and write any file that the user running it can access.

### Memory

| # | Name | Arguments | Result |
|---|---|---|---|
| 20 | Allocate | R0 = size | R0 = the address. On failure, R0 = 0 and R5 = 8 |
| 21 | Free | R0 = address | R0 = the status, also in R5 |
| 22 | Copy | R0 = destination, R5 = source, R6 = count | R0 = count, or 0 on failure |
| 23 | Memory info | | R0 = memory size, R6 = free heap bytes, R7 = largest free block |

### Process and random numbers

| # | Name | Arguments | Result |
|---|---|---|---|
| 30 | Exit | R0 = exit status | Stops the program |
| 31 | Sleep | R0 = milliseconds | |
| 32 | Time | | R0 = milliseconds since start |
| 33 | Ticks | | R0 = instructions executed |
| 34 | Argument | R0 = index, R5 = buffer, R6 = buffer size | Copies argument *index* into the buffer, truncating it to fit. R0 = the argument's full length, or 0xFFFFFFFF if there is no such argument. With a size of 0, only the length is returned |
| 35 | Abort | R0 = NUL-terminated message, R5 = number of calls to leave out | Stops the program the way a fault does, with the message. The report starts where the call that many levels out was made, so a runtime's error routine can blame its caller |
| 40 | Random | R0 = limit | R0 = a number below the limit, or any 32-bit value if the limit is 0 |
| 41 | Seed | R0 = seed | |

Argument 0 is the program path as given to `vm`.

The random generator is xorshift32 with a fixed default seed. Runs are therefore reproducible unless the program seeds it.

### Error codes

These codes appear in R5 after syscalls and in `CPUID` function 4. Codes 1 to 14 are also the [exception](#interrupts-and-exceptions) vectors of those faults.

| Code | Meaning |
|---|---|
| 0 | No error |
| 1 | Invalid instruction |
| 2 | Segmentation fault |
| 3 | Stack overflow |
| 4 | Stack underflow |
| 5 | Division by zero |
| 6 | Invalid memory address |
| 7 | Invalid system call |
| 8 | Memory allocation error |
| 9 | Alignment error |
| 10 | Unhandled interrupt |
| 11 | I/O error or end of input |
| 12 | Memory protection fault |
| 13 | Privilege violation |
| 14 | Page fault |
| 15 | Single-step trap (a vector only) |
| 16 | Instruction limit reached (never delivered to the program) |
| 17 | Stopped by a signal (never delivered to the program) |
| 18 | Aborted by the program with syscall 35 (never delivered to the program) |

## Debugger

`vm -d program.bin` stops before the first instruction. It shows the current source line, with the lines around it when the source file is available.

| Command | Effect |
|---|---|
| `s`, `step [N]` | Execute N instructions (default 1) |
| `n`, `next` | Run to the next source line, stepping over calls |
| `f`, `finish` | Run until the current subroutine returns |
| `c`, `continue` | Run until a breakpoint, a `DEBUG` instruction, an exception, `HALT` or a fault |
| `b`, `break ADDR\|SYMBOL` | Set a breakpoint, for example `b main.loop`, `b 0x10` or `b count.c:4` |
| `w`, `watch ADDR\|SYMBOL` | Stop after an instruction changes the 32-bit word at ADDR |
| `d`, `delete N` | Delete breakpoint or watchpoint N |
| `lb`, `breakpoints` | List breakpoints and watchpoints |
| `ls`, `symbols` | List symbols |
| `x`, `disas [ADDR] [N]` | Disassemble N instructions (default: 8 at PC) |
| `m`, `memory ADDR [N]` | Dump N bytes (default 16) |
| `stack [N]` | Show N words from the top of the stack (default 8) |
| `bt`, `backtrace` | Show the calls and interrupts that led to the current instruction |
| `r`, `registers` | Show registers and flags |
| `cr` | Show control registers |
| `h`, `help` | Show help |
| `q`, `quit` | Leave the debugger |

Commands that take an address also take a symbol or `FILE:LINE`, which stands for the lowest address of that line's code. The file name may leave out leading directories.

Any command that runs the program stops when an exception is delivered to a handler, and names the exception. The program's input and output share the terminal with the debugger.

`vm -x FILE program.bin` takes the commands from FILE instead, which leaves stdin to the program. Each command is echoed after its prompt, lines starting with `#` are skipped, and the debugger quits at the end of the file:

```console
$ printf 'b read_done\nc\nm line 8\n' > commands.txt
$ ./vm -x commands.txt program.bin < input.txt
```

## Binary format

A VM32 file contains, in order:

1. A header.
2. The code bytes, loaded at the code base.
3. The data bytes, loaded at the data base.
4. Optional debug information.

All fields are little-endian.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | Magic `VM32` |
| 4 | 2 | Major version (2) |
| 6 | 2 | Minor version (0) |
| 8 | 4 | Header size (36) |
| 12 | 4 | Code base |
| 16 | 4 | Code size |
| 20 | 4 | Data base |
| 24 | 4 | Data size |
| 28 | 4 | Size of the debug information |
| 32 | 4 | Entry point |

The VM loads code and data at their bases, which must leave room for the stack at the top of memory. It rejects other major versions, since their instruction encoding differs.

The debug information holds the symbols, then the source lines. Each string is stored as a 16-bit length followed by its bytes:

```
u32 symbol count
    string name, u32 value, u8 kind (0 code, 1 data, 2 constant), u32 line, string file
u32 line count
    u32 address, u32 line, string source text, string file
```

Object files from `vmasm -c` start with `VMOB` and hold both sections, the symbols, the places the linker fills in and the source lines:

```
"VMOB", u16 version (1), u16 flags (1: sets an entry point), u32 entry offset in the code
u32 code size, u32 code alignment, u32 data size, u32 data alignment, code bytes, data bytes
u32 symbol count
    string name, u8 kind (0 code, 1 data, 2 constant, 3 external), u8 exported, u32 value, u32 line, string file
u32 relocation count
    u8 section (0 code, 1 data), u8 type, u32 offset, u32 target, u32 addend
u32 line count
    u8 section, u32 offset, u32 line, string source text, string file
```

Code and data symbols hold offsets into their section. A relocation stores the address of its target plus the addend at the offset. For type 1 it stores that minus the address of the word after it, as jumps expect. The target is the file's own code (0) or data (1), or 2 plus the index of a symbol.

## Tests

`make test` runs `tests/run.sh`, which does four things:

- Assembles and runs every program in `tests/programs`. A program's output must match `NAME.out`, and `NAME.in`, if present, is fed to its stdin.
- Checks that every file in `tests/errors` fails to assemble with the expected message.
- Compiles and runs every Ore program in `ore/tests` the same way. Their comment lines start with `//` instead of `;`, and `// vmc-args:` gives `vmc0` more arguments.
- Checks that every file in `ore/tests/errors` fails to compile with the expected message.

Comment lines in a test adjust the checks:

| Line | Meaning |
|---|---|
| `; expect-exit: N` | Expected exit status (default 0) |
| `; expect-stderr: text` | Text that must appear on stderr |
| `; expect-error: text` | Expected assembler or compiler error (only in `tests/errors` and `ore/tests/errors`) |
| `; expect-warning: text` | A warning of `vmasm -W` that has to appear. Programs are assembled with `-W`, and every warning needs such a line |
| `; link: modules/a.asm ...` | Assemble the program and these files from `tests/programs` with `-c`, and link them |
| `; expect-link-error: text` | Expected linker error |
| `; asm-args: ...` | Extra arguments for the assembler |
| `; vm-args: ...` | Extra options for the VM |
| `; program-args: ...` | Arguments passed to the program |
| `; disk-sectors: N` | Attach an empty disk image of N sectors |

A file `NAME.x` next to a test holds [debugger](#debugger) commands, which the program then runs under with `-x`, and `NAME.keys` is passed as a key script with `-k`. When `NAME.err` exists, stderr has to match it as a whole.

`make test T="display disk"` or `sh tests/run.sh display disk` runs only the tests named. `sh tests/run.sh -u NAME` rewrites `NAME.out`, and `NAME.err` if there is one, from the actual output, provided the program exits with the expected status; a failing test shows the first lines of the difference with control characters made visible.

Programs run inside a temporary directory, so any files they create are discarded. They also run with a limit of 10 million instructions, so a program stuck in a loop fails instead of hanging the suite. The `example_*` tests include the programs from `assembler/examples`.

## Source layout

| Path | Contents |
|---|---|
| `src/main.c` | The `vm` command line and fault reports |
| `src/monitor.c` | Tracing and profiling while a program runs |
| `src/vm.c` | Machine setup, program loading and the fetch-execute step |
| `src/debugger.c` | The interactive debugger |
| `src/core/` | CPU helpers, instruction execution, memory and heap, syscalls, disassembler, debug info |
| `src/io/` | The I/O devices: console, timer, display, keyboard and disk |
| `src/common/` | Instruction table, encoding, byte buffers and the binary format, shared by all three tools |
| `assembler/` | `vmasm`: lexer, expressions, symbols, includes and macros, the two passes, output and the register check |
| `linker/` | `vmld` |
| `assembler/examples/` | Example programs |
| `ore/bootstrap/` | `vmc0`: lexer, parser, type checker and code generator for Ore |
| `ore/lib/` | The runtime that compiled Ore programs start from, and the standard library in `std/` |
| `ore/tests/` | Ore test programs, and the modules and assembly they use |
| `docs/language.md` | The Ore language |
| `include/` | Headers |
| `tests/` | Test programs and the test runner |
