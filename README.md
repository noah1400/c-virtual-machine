# VM - Virtual Machine Implementation

This is a virtual machine (VM) implementation featuring a RISC-like instruction set, a disassembler, debugger, and assembler. The VM includes support for interrupts, various syscalls, and memory management.

## Table of Contents

- [Building the VM](#building-the-vm)
- [Using the VM](#using-the-vm)
- [Assembly Language](#assembly-language)
  - [Registers](#registers)
  - [Memory Layout](#memory-layout)
  - [Addressing Modes](#addressing-modes)
  - [Instruction Format](#instruction-format)
  - [Instruction Set](#instruction-set)
- [Using the Assembler](#using-the-assembler)
  - [Assembler Directives](#assembler-directives)
  - [Assembly Example](#assembly-example)
- [Syscalls](#syscalls)
  - [Console I/O (0-9)](#console-io-0-9)
  - [File Operations (10-19)](#file-operations-10-19)
  - [Memory Operations (20-29)](#memory-operations-20-29)
  - [Process Control (30-39)](#process-control-30-39)
  - [Random Number Generation (40-49)](#random-number-generation-40-49)
- [Debugging](#debugging)

## Building the VM

To build the VM, simply run:

```bash
make
```

This will compile the VM executable named `vm`.

## Using the VM

To run a program:

```bash
./vm <input_file>
```

To disassemble a binary file:

```bash
./vm -D <input_file>
```

To run in debug mode:

```bash
./vm -d <input_file>
```

For help:

```bash
./vm -h
```

To specify the VM memory size:

```bash
./vm -m 128 <input_file>  # Sets memory to 128KB
```

## Assembly Language

### Registers

The VM has 16 general-purpose 32-bit registers:

| Register | Alias   | Description               |
|----------|---------|---------------------------|
| R0       | ACC     | Accumulator               |
| R1       | BP      | Base pointer              |
| R2       | SP      | Stack pointer             |
| R3       | PC      | Program counter           |
| R4       | SR      | Status register (flags)   |
| R5-R14   |         | General purpose registers |
| R15      | LR      | Link register             |

The status register (R4) contains flags:

- ZERO_FLAG (Z): Set when result is zero
- NEG_FLAG (N): Set when result is negative
- CARRY_FLAG (C): Set when carry occurs
- OVER_FLAG (O): Set when overflow occurs
- INT_FLAG (I): Interrupts enabled
- DIR_FLAG (D): Direction flag
- SYS_FLAG (S): System mode flag
- TRAP_FLAG (T): Debug trap flag

### Memory Layout

The memory is divided into four segments:

| Segment | Base Address | Size  | Purpose       |
|---------|--------------|-------|---------------|
| Code    | 0x0000       | 16KB  | Program code  |
| Data    | 0x4000       | 16KB  | Static data   |
| Stack   | 0x8000       | 16KB  | Stack         |
| Heap    | 0xC000       | 16KB  | Dynamic data  |

### Addressing Modes

The VM supports these addressing modes:

| Mode | Name             | Syntax                 | Description                        |
|------|------------------|------------------------|------------------------------------|
| 0    | Immediate (IMM)  | #123, #0xFF           | Value is part of the instruction   |
| 1    | Register (REG)   | R0, ACC               | Value is in a register             |
| 2    | Memory (MEM)     | [1234], [0xFF]        | Direct memory address              |
| 3    | Reg. Indirect    | [R0], [R5]            | Address in register                |
| 4    | Indexed          | [R0+123], [R5+0xFF]   | Base register plus offset          |
| 5    | Stack Relative   | [SP+4], [SP+0x10]     | Stack pointer plus offset          |
| 6    | Base Relative    | [BP+4], [BP+0x10]     | Base pointer plus offset           |

### Instruction Format

Instructions are 32-bit with the following format:
```
[Opcode: 8 bits][Mode: 4 bits][Reg1: 4 bits][Reg2: 4 bits][Immediate/Offset: 12 bits]
```

### Instruction Set

The VM supports various instruction categories:

- **Data Transfer**: LOAD, STORE, MOVE, LOADB, STOREB, LOADW, STOREW, LEA
- **Arithmetic**: ADD, SUB, MUL, DIV, MOD, INC, DEC, NEG, CMP, ADDC, SUBC
- **Logical**: AND, OR, XOR, NOT, SHL, SHR, SAR, ROL, ROR, TEST
- **Control Flow**: JMP, JZ, JNZ, JN, JP, JO, JC, JBE, JA, CALL, RET, SYSCALL, LOOP
- **Stack**: PUSH, POP, PUSHF, POPF, PUSHA, POPA, ENTER, LEAVE
- **System**: HALT, INT, CLI, STI, IRET, IN, OUT, CPUID, RESET, DEBUG
- **Memory Control**: ALLOC, FREE, MEMCPY, MEMSET, PROTECT

## Using the Assembler

The assembler is written in Python and can be found in `assembler/assembler.py`.

To assemble a program:

```bash
python3 assembler/assembler.py input.asm -o output.bin
```

### Assembler Directives

| Directive  | Description                                  | Example                     |
|------------|----------------------------------------------|----------------------------|
| .text      | Start code section                          | `.text`                    |
| .data      | Start data section                          | `.data`                    |
| .byte      | Define byte values                          | `.byte 1, 2, 3`            |
| .word      | Define 16-bit values                        | `.word 0x1234, 0x5678`     |
| .dword     | Define 32-bit values                        | `.dword 0x12345678`        |
| .ascii     | Define ASCII string                         | `.ascii "Hello"`           |
| .asciiz    | Define null-terminated string               | `.asciiz "Hello, World!"`  |
| .space     | Reserve space                               | `.space 16`                |
| .align     | Align to boundary                           | `.align 4`                 |
| .equ       | Define constant                             | `.equ BUFSIZE, 1024`       |
| .org       | Set current address                         | `.org 0x4100`              |
| .include   | Include another file                        | `.include "macros.asm"`    |

### Assembly Example

Here's a simple factorial program:

```assembly
; Example assembly program for the VM
; This program calculates the factorial of a number

.text                   ; Code section
    ; Initialize registers
    LOAD R5, #5         ; Number to calculate factorial of
    LOAD R0, #1         ; Initialize result to 1
    
    ; Check for special case of factorial 0 (result is 1)
    CMP R5, #0
    JZ done
    
loop:
    ; Multiply accumulator by the counter
    MUL R0, R5
    
    ; Decrement counter
    DEC R5
    
    ; Check if we're done
    CMP R5, #0
    JNZ loop            ; Continue if counter > 0
    
done:
    ; Print the result
    PUSH R0             ; Save the result
    ; Print newline
    LOAD R0, #10        ; ASCII for newline
    SYSCALL #0          ; Syscall 0 = print character

    ; Print message
    LOAD R0, message    ; Load address of message
    SYSCALL #2          ; Syscall 2 = print string

    ; Print the result
    POP R0              ; Restore the result
    SYSCALL #1          ; Syscall 1 = print integer (R0 has the value)

    ; Print newline
    LOAD R0, #10        ; ASCII for newline
    SYSCALL #0          ; Syscall 0 = print character

    ; Exit program
    HALT

.data                   ; Data section
message:
    .asciiz "Factorial result: "  ; Null-terminated string
```

## Syscalls

Syscalls are invoked using the `SYSCALL` instruction with an immediate value specifying the syscall number. Parameters are passed in registers R0_ACC, R5, R6, and R7. Return values are placed in R0_ACC and error codes in R5.

### Console I/O (0-9)

| Number | Description         | Parameters                       | Returns          |
|--------|---------------------|----------------------------------|------------------|
| 0      | Print character     | R0_ACC = character code          | None             |
| 1      | Print integer       | R0_ACC = integer to print        | None             |
| 2      | Print string        | R0_ACC = string address          | None             |
| 3      | Read character      | None                             | R0_ACC = char    |
| 4      | Read string         | R0_ACC = buffer addr, R5 = maxlen| R0_ACC = chars read |
| 5      | Print hex integer   | R0_ACC = value                   | None             |
| 6      | Print formatted int | R0_ACC = value, R5 = base        | None             |
| 7      | Print float         | R0_ACC = fixed-point (16.16)     | None             |
| 8      | Clear screen        | None                             | None             |
| 9      | Set console color   | R0_ACC = color code              | None             |

### File Operations (10-19)

| Number | Description         | Parameters                        | Returns          |
|--------|---------------------|-----------------------------------|------------------|
| 10     | File open           | R0_ACC = filename addr, R5 = mode | R0_ACC = handle  |
| 11     | File close          | R0_ACC = file handle              | R0_ACC = success |
| 12     | File read           | R0_ACC = handle, R5 = addr, R6 = count | R0_ACC = bytes read |
| 13     | File write          | R0_ACC = handle, R5 = addr, R6 = count | R0_ACC = bytes written |

### Memory Operations (20-29)

| Number | Description         | Parameters                      | Returns          |
|--------|---------------------|--------------------------------|------------------|
| 20     | Allocate memory     | R0_ACC = size                  | R0_ACC = address |
| 21     | Free memory         | R0_ACC = address               | R0_ACC = result  |
| 22     | Copy memory         | R0_ACC = dest, R5 = src, R6 = count | R0_ACC = bytes copied |
| 23     | Memory information  | None                           | R0_ACC = total memory size |

### Process Control (30-39)

| Number | Description         | Parameters                      | Returns          |
|--------|---------------------|--------------------------------|------------------|
| 30     | Exit program        | R0_ACC = exit code             | Does not return  |
| 31     | Sleep               | R0_ACC = milliseconds          | None             |
| 32     | Get system time     | None                           | R0_ACC = time in ms |
| 33     | Performance counter | None                           | R0_ACC = instruction count |

### Random Number Generation (40-49)

| Number | Description         | Parameters                      | Returns          |
|--------|---------------------|--------------------------------|------------------|
| 40     | Get random number   | R0_ACC = max value             | R0_ACC = random number |
| 41     | Seed RNG            | R0_ACC = seed value            | None             |

## Debugging

When running in debug mode (`./vm -d program.bin`), you can use these commands:

- `s [N]` - Step N instructions (default: 1)
- `r` - Run until halt
- `q` - Quit
- `m ADDR N` - Dump N bytes of memory at ADDR
- `h` - Show help
- `b` - Run until next breakpoint (DEBUG instruction)

Breakpoints can be set programmatically by using the `DEBUG` instruction in your assembly code.