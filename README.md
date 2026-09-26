# VM32

A 32-bit virtual machine written in C, with its own assembler, disassembler and interactive debugger.

- **`vm`** runs VM32 programs and can trace, disassemble or debug them.
- **`vmasm`** turns assembly source into VM32 binaries. It supports expressions, local labels, macros, conditional assembly, includes and listings.

## Contents

- [Building](#building)
- [Quick start](#quick-start)
- [Running programs](#running-programs)
- [The machine](#the-machine)
- [Instruction set](#instruction-set)
- [Assembly language](#assembly-language)
- [Syscalls](#syscalls)
- [Debugger](#debugger)
- [Binary format](#binary-format)
- [Tests](#tests)
- [Source layout](#source-layout)

## Building

You need a C11 compiler and a POSIX system.

```sh
make          # builds ./vm and ./vmasm
make test     # builds both and runs the test suite
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

`assembler/examples` has more programs. They range from a Fibonacci printer to `MiniDos`, a small command shell that you assemble from `MiniDos/main.asm`. `cat.asm` prints the files named on its command line:

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
| `-d` | Start the interactive [debugger](#debugger) |
| `-D` | Disassemble the program instead of running it |
| `-m KB` | Memory size in KB, 64 to 65536 (default 64) |
| `-n COUNT` | Stop with an error after COUNT instructions |
| `-t` | Print each instruction on stderr before it executes |
| `-v` | Print loading and execution statistics on stderr |
| `-h` | Show help |

The exit status is:

- 0 after `HALT`.
- The low 8 bits of the code passed to [syscall 30](#process-and-random-numbers).
- 1 when the program faults or cannot be loaded.

A fault stops the machine. The report names the faulting instruction, and its source line too when the binary carries debug information:

```console
$ ./vm fault.bin
vm: error: Division by zero
vm: at 0x0004 fault.asm:5: DIV R8, #0x0
```

A trace shows the address, the nearest label and the instruction:

```console
$ ./vm -t hello.bin 2>&1 >/dev/null | head -4
0x0000 <main>               LOAD R0, #0x4000  ; greeting
0x0004 <main+4>             SYSCALL #2
0x0008 <main+8>             LOAD R8, #0x0
0x000C <main+12>            LOAD R9, #0xA
```

`-D` prints the header, then the code with its labels, then a hex dump of the data segment. A file without a VM32 header is treated as raw code and loaded at address 0.

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
| 0x20–0x80 | D, S, T | Reserved |

### Memory

Memory is byte-addressed and little-endian. Unaligned data accesses are allowed. The first 64 KB are split into four 16 KB segments:

| Range | Segment | Contents |
|---|---|---|
| 0x0000–0x3FFF | Code | `.text`. The interrupt vector table is at 0x0100–0x04FF |
| 0x4000–0x7FFF | Data | `.data` |
| 0x8000–0xBFFF | Stack | SP and BP start at 0xC000, and the stack grows down |
| 0xC000–0xFFFF | Heap | Blocks from `ALLOC` or syscall 20 |

Memory added with `-m` starts at 0x10000. Instruction immediates are only 16 bits wide, so that memory can only be reached through a register.

Every access is bounds-checked:

- Code, data and stack have no protection.
- An access that touches the heap must stay inside one allocated block and respect that block's protection.
- Pushing below 0x8000 or popping above 0xC000 faults with stack overflow or underflow.
- So does any stack operation while SP points outside the stack segment.

### Heap

The heap is a first-fit allocator. Its 8-byte block headers live in VM memory.

- Sizes are rounded up to a multiple of 4, with a minimum of 8 bytes.
- A single allocation can be at most 16376 bytes.
- New blocks are zero-filled.
- A freed block is merged with free neighbours.
- Freeing a block twice, or freeing an address that is not the start of a block, is an error.

`PROTECT` sets a block's permissions: 1 read, 2 write, 4 execute. New blocks get all three. Code only runs from the heap if its block has execute permission.

### Interrupts

The interrupt vector table holds one 32-bit handler address per vector. The entry for vector *v* is at `0x0100 + 4*v`.

When an interrupt is taken, the CPU:

1. Pushes all registers as `PUSHA` does. Afterwards `[SP + 4*n]` holds R*n*, and the saved PC is the return address.
2. Clears I.
3. Jumps to the handler.

`IRET` restores every register except SP, including PC and the flags. A handler can read or change the interrupted registers through that saved frame.

- **Software interrupts:** `INT #v` raises vector *v* at once, whether or not I is set.
- **Device interrupts:** these are latched, then delivered before the next instruction once I is set (`STI`).
- **Missing handlers:** a zero entry in the table faults with "Unhandled interrupt".

The table overlaps the code segment. A program that uses interrupts either reserves its entries with `.org`, as `assembler/examples/interrupts.asm` does, or stores handler addresses at run time.

### I/O ports

`IN Rd, port` and `OUT port, Rs` take the port as an immediate or a register. Accessing a port that has no device faults.

| Port | Device | IN | OUT |
|---|---|---|---|
| 0x00 | Console | Next byte from stdin, or 0 at end of input | Writes the low byte to stdout |
| 0x01 | Console | 0 | Writes the low byte to stderr |
| 0x40 | Timer interval | Current interval | Sets the number of instructions between timer interrupts; 0 stops the timer |
| 0x41 | Timer vector | Current vector | Sets the vector raised on expiry (0–255) |
| 0x42 | Timer ticks | Expirations so far | Sets the counter |

`assembler/examples/timer.asm` drives a main loop from timer interrupts.

## Instruction set

### Encoding

Each instruction is one little-endian 32-bit word at a 4-byte aligned address. Jumping to an unaligned address faults.

```
 31        24 23    20 19    16 15    12 11                0
+------------+--------+--------+--------+-------------------+
|   opcode   |  mode  |   r1   |   r2   |     immediate     |
+------------+--------+--------+--------+-------------------+
```

In the IMM, MEM, STK and BAS modes, the r2 field holds the upper 4 bits of a 16-bit immediate. The top three bits of the opcode select the instruction group.

### Addressing modes

| Mode | Syntax | Operand | Range |
|---|---|---|---|
| IMM | `#expr` or `expr` | The value itself, zero-extended | 0 to 65535 |
| REG | `R5` | The register | |
| MEM | `[expr]` | Memory at an absolute address | 0 to 0xFFFF |
| REGM | `[R5]` | Memory at the address in a register | |
| IDX | `[R5+expr]`, `[R5-expr]` | Register plus displacement | −2048 to 2047 |
| STK | `[SP]`, `[SP+expr]` | SP plus displacement | −32768 to 32767 |
| BAS | `[BP]`, `[BP-expr]` | BP plus displacement | −32768 to 32767 |

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
| `LOADHI Rd, #imm` | 0x09 | Replaces the upper 16 bits of Rd |

`LOAD Rd, #value` accepts any 32-bit constant, including negative ones. If the constant does not fit in 16 bits, the assembler emits `LOAD` followed by `LOADHI`. The value must be known when the assembler reaches that line, so define large constants before you use them.

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

Division by zero faults. So does signed division of −2147483648 by −1.

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

- A label or address, as in `JMP loop`.
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
| `RESET` | 0xA8 | Clears the registers, resets SP and BP, and jumps to the entry point. Memory is kept |
| `DEBUG` | 0xA9 | A breakpoint under `vm -d`. Does nothing otherwise |

| R0 | `CPUID` result |
|---|---|
| 0 | R0 = highest function (4). R5 and R6 = the vendor string "VM32CPU" |
| 1 | R0 = version, 0x00010001. R5 and R6 = feature bits |
| 2 | R0 = memory size. R5 = segment bases / 256, one byte per segment. R6 = segment sizes in KB |
| 3 | R0 = number of instructions. R5 = mask of addressing modes. R6 = mask of instruction groups |
| 4 | R0 = instructions executed. R5 = 2 under the debugger, plus 4 while I is set. R6 = the last error code |

### Memory management

| Instruction | Opcode | Operation |
|---|---|---|
| `ALLOC Rd, val` | 0xC0 | Rd = the address of a new zero-filled block of *val* bytes. Faults if the heap is exhausted |
| `FREE Rs` | 0xC1 | Frees the block at Rs |
| `MEMCPY Rd, Rs, size` | 0xC2 | Copies *size* bytes from Rs to Rd. The ranges may overlap |
| `MEMSET Rd, Rv, size` | 0xC3 | Fills *size* bytes at Rd with the low byte of Rv |
| `PROTECT Ra, val` | 0xC4 | Sets the permissions of the block at Ra |

*size* is an immediate from 0 to 4095, or a register. Syscalls 20 to 22 do the same jobs, but they report failures in R5 instead of faulting.

## Assembly language

```
label:  MNEMONIC operand, operand   ; comment
```

- Each line holds at most one statement, which can follow a label. Comments start with `;`.
- Mnemonics, registers and directives are case-insensitive. Symbols are case-sensitive.
- A label that starts with a dot is local to the global label before it. For example, `.loop` after `main:` defines `main.loop`, and other code can reach it under that full name.
- Instructions go in `.text`, which starts at 0x0000. Data goes in `.data`, which starts at 0x4000. You can switch between the two sections as often as you like.

### Expressions

Operands and directive arguments are integer expressions. An expression can contain:

- **Numbers:** `42`, `0x2A`, `0b101010`, `0o52`, or C-style octal such as `052`.
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
| `.ascii "s", ...` | String bytes |
| `.asciiz "s", ...` or `.string` | NUL-terminated strings |
| `.space n[, fill]` or `.skip` | *n* bytes of *fill* (default 0) |
| `.align n` | Pad to a multiple of *n*, a power of two up to 4096 |
| `.org address` | Pad forward to an address in the current section |
| `.equ NAME, expr` or `.set` | Define a constant. A constant cannot be redefined |
| `.entry expr` | Start execution here instead of at 0x0000 |
| `.error "message"` | Stop assembly with this message. Useful inside `.if` |
| `.include "file"` | Insert a file. It is searched for next to the including file, then in `-I` directories |
| `.macro` ... `.endm` | Define a macro |
| `.if`, `.ifdef`, `.ifndef`, `.else`, `.endif` | Conditional assembly |

A value can be written signed or unsigned, but it must fit its width. For example, `.byte` accepts −128 to 255.

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
| `-o FILE` | Output file (default: the input with a `.bin` extension) |
| `-l FILE` | Write a listing: line number, address, encoded bytes and source. Lines from macros are marked with `+` |
| `-I DIR` | Also search DIR for included files |
| `-D NAME[=VALUE]` | Define a constant, 1 unless a value is given |
| `-s` | Print the symbol table |
| `-S` | Leave out debug information |
| `-h` | Show help |

`-I` and `-D` also accept their value attached, as in `-DNAME`.

By default, a binary includes the symbols and source lines that the disassembler, the debugger and fault reports use. Errors are reported as `file:line: error: message`, and vmasm then exits with status 1.

## Syscalls

`SYSCALL #n` takes its arguments in R0, R5, R6 and R7 and returns its result in R0. Every syscall also sets R5 to a status: 0 on success, otherwise one of the [error codes](#error-codes).

- Console syscalls fault when given an invalid address.
- File and memory syscalls report problems in R5 instead, and the program keeps running.
- An unknown syscall number faults.

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
| 7 | Print fixed-point | R0 = signed 16.16 value | Printed with four decimals |
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
| 40 | Random | R0 = limit | R0 = a number below the limit, or any 32-bit value if the limit is 0 |
| 41 | Seed | R0 = seed | |

Argument 0 is the program path as given to `vm`.

The random generator is xorshift32 with a fixed default seed. Runs are therefore reproducible unless the program seeds it.

### Error codes

These codes appear in R5 after syscalls and in `CPUID` function 4.

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
| 14 | Instruction limit reached |

## Debugger

`vm -d program.bin` stops before the first instruction. It shows the current source line, with the lines around it when the source file is available.

| Command | Effect |
|---|---|
| `s`, `step [N]` | Execute N instructions (default 1) |
| `n`, `next` | Run to the next source line, stepping over calls |
| `f`, `finish` | Run until the current subroutine returns |
| `c`, `continue` | Run until a breakpoint, a `DEBUG` instruction, `HALT` or a fault |
| `b`, `break ADDR\|SYMBOL` | Set a breakpoint, for example `b main.loop` or `b 0x10` |
| `w`, `watch ADDR\|SYMBOL` | Stop after an instruction changes the 32-bit word at ADDR |
| `d`, `delete N` | Delete breakpoint or watchpoint N |
| `lb`, `breakpoints` | List breakpoints and watchpoints |
| `ls`, `symbols` | List symbols |
| `x`, `disas [ADDR] [N]` | Disassemble N instructions (default: 8 at PC) |
| `m`, `memory ADDR [N]` | Dump N bytes (default 16) |
| `stack [N]` | Show N words from the top of the stack (default 8) |
| `r`, `registers` | Show registers and flags |
| `h`, `help` | Show help |
| `q`, `quit` | Leave the debugger |

The program's input and output share the terminal with the debugger.

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
| 4 | 2 | Major version (1) |
| 6 | 2 | Minor version (1) |
| 8 | 4 | Header size (36) |
| 12 | 4 | Code base |
| 16 | 4 | Code size |
| 20 | 4 | Data base |
| 24 | 4 | Data size |
| 28 | 4 | Size of the debug information |
| 32 | 4 | Entry point |

Version 1.0 files have a 32-byte header without the entry point. They start at the code base.

The debug information holds the symbols, then the source lines. Each string is stored as a 16-bit length followed by its bytes:

```
u32 symbol count
    string name, u32 value, u8 kind (0 code, 1 data, 2 constant), u32 line, string file
u32 line count
    u32 address, u32 line, string source text, string file
```

## Tests

`make test` runs `tests/run.sh`, which does two things:

- Assembles and runs every program in `tests/programs`. A program's output must match `NAME.out`, and `NAME.in`, if present, is fed to its stdin.
- Checks that every file in `tests/errors` fails to assemble with the expected message.

Comment lines in a test adjust the checks:

| Line | Meaning |
|---|---|
| `; expect-exit: N` | Expected exit status (default 0) |
| `; expect-stderr: text` | Text that must appear on stderr |
| `; expect-error: text` | Expected assembler error (only in `tests/errors`) |
| `; asm-args: ...` | Extra arguments for the assembler |
| `; vm-args: ...` | Extra options for the VM |
| `; program-args: ...` | Arguments passed to the program |

Programs run inside a temporary directory, so any files they create are discarded. They also run with a limit of 10 million instructions, so a program stuck in a loop fails instead of hanging the suite. The `example_*` tests include the programs from `assembler/examples`.

## Source layout

| Path | Contents |
|---|---|
| `src/main.c` | The `vm` command line, tracing and fault reports |
| `src/vm.c` | Machine setup, program loading and the fetch-execute step |
| `src/debugger.c` | The interactive debugger |
| `src/core/` | CPU helpers, instruction execution, memory and heap, syscalls, disassembler, debug info |
| `src/io/` | The I/O devices: console and timer |
| `src/common/` | Instruction table, encoding and binary format, shared with the assembler |
| `assembler/` | `vmasm`: lexer, expressions, symbols, includes and macros, the two passes, output |
| `assembler/examples/` | Example programs |
| `include/` | Headers |
| `tests/` | Test programs and the test runner |
