# Ore

Ore is a small systems language for VM32. It keeps C's model of the machine: pointers, structs, manual memory and no hidden costs. Declarations read left to right, files are modules instead of headers, slices check their bounds, and a conversion that can lose information has to be written out.

`vmc` compiles Ore to VM32 assembly with [`.loc`](../README.md#directives) lines, so faults, backtraces, the debugger and coverage listings all point at Ore source lines.

```
struct Stats {
    count: int;
    largest: int;
}

// Counts the primes below limit with a sieve
fn primes(limit: int) Stats {
    var composite = new(bool, limit);
    var stats = Stats {};
    for (var n = 2; n < limit; n += 1) {
        if (!composite[n]) {
            stats.count += 1;
            stats.largest = n;
            for (var m = n * n; m < limit; m += n) {
                composite[m] = true;
            }
        }
    }
    free(composite);
    return stats;
}

fn main() int {
    var stats = primes(1000);
    print(stats.count, " primes below 1000, the largest is ", stats.largest, "\n");
    return 0;
}
```

```console
$ ./vmc primes.ore
$ ./vm primes.bin
168 primes below 1000, the largest is 997
```

## Lexical elements

- **Comments** run from `//` to the end of the line, or from `/*` to the next `*/`.
- **Identifiers** start with a letter or `_` and continue with letters, digits and `_`.
- **Integer literals** are decimal (`255`), hexadecimal (`0xFF`) or binary (`0b1111_1111`). `_` may separate digits. Decimal literals other than `0` do not start with 0.
- **Float literals** have a fraction or an exponent: `1.5`, `2e-3`, `1_000.25`. They round to the nearest `f32`, halfway cases to the even one, and one too large for an `f32` does not compile.
- **Character literals** such as `'a'` are integers with the character's code.
- **String literals** such as `"hi\n"` are byte slices.
- **Escapes**, in both characters and strings: `\n`, `\t`, `\r`, `\0`, `\\`, `\'`, `\"` and `\xNN`.
- **Statements** end with `;`.

Keywords: `as break case const continue default else enum extern false fn for if import interrupt null pub return struct switch true var while`.

## Types

| Type | Values |
|---|---|
| `i8`, `i16`, `i32` | Signed integers of 8, 16 and 32 bits |
| `u8`, `u16`, `u32` | Unsigned integers of 8, 16 and 32 bits |
| `int` | Another name for `i32` |
| `bool` | `true` and `false` |
| `f32` | Single-precision floats |
| `*T` | Pointer to a `T`, or `null` |
| `[N]T` | Array of *N* values of type `T` |
| `[]T` | Slice: a pointer to `T` values and their number |
| `fn(A, B) R` | Pointer to a function, or `null` |
| `struct` and `enum` types | Declared by name, see [Declarations](#declarations) |

Strings are `[]u8`. There is no separate character type.

**Pointers** point at one value, but `p[i]`, `p + i` and `p - i` step through memory by whole elements, and `p - q` counts the elements between two pointers. None of this is checked; slices are the checked way to handle many values.

**Arrays** are values. Assigning an array or passing it to an `[N]T` parameter copies it. `a.len` is the constant *N*.

**Slices** have two fields, `s.ptr` and `s.len` (an `int`). They do not own their memory; they refer to an array, to memory from `new`, or to a string literal. Slicing makes a new slice:

| Expression | Result |
|---|---|
| `x[i:j]` | Elements *i* up to, not including, *j* of an array, slice or pointer |
| `x[:j]`, `x[i:]`, `x[:]` | A missing start is 0 and a missing end is `x.len`. Pointers have no length, so slicing one needs an end |

String literals end with a 0 byte that the slice does not include, so `"text".ptr` can go to code that expects a NUL-terminated string.

### Layout

Values are laid out as in C, so they can be shared with assembly:

| Type | Size | Alignment |
|---|---|---|
| `i8`, `u8`, `bool` | 1 | 1 |
| `i16`, `u16` | 2 | 2 |
| `i32`, `u32`, `f32`, pointers, functions, enums | 4 | 4 |
| `[]T` | 8: the pointer, then the length | 4 |
| `[N]T` | *N* times the size of `T` | That of `T` |
| `struct` | Fields in order, each at its alignment, rounded up to the largest alignment | The largest of its fields |

Variables and memory from `new` start out as zero unless initialized: 0, `false`, `null`, an empty slice.

## Declarations

A module declares constants, variables, types and functions, in any order. `pub` in front of one lets other modules use it.

```
const WIDTH = 80;                   // a constant, no storage
const LIMIT: u8 = 200;              // a constant of a given type
var counter: int;                   // a global variable, 0 at the start
pub var table = [4]u8 { 1, 2, 4, 8 };

pub struct Point {
    x: int;
    y: int;
}

enum Color { red, green, blue = 7, gray }   // gray is 8

pub fn distance2(a: Point, b: Point) int {
    var dx = a.x - b.x;
    var dy = a.y - b.y;
    return dx * dx + dy * dy;
}
```

- **Constants** hold integers, floats, booleans or strings, and their value must be known when compiling.
- **Global variables** take constant initializers: literals, constants, strings, addresses of globals, function names, and array and struct literals made of those.
- **Structs** may contain themselves only through pointers, as in `next: *Node`.
- **Enums** are types of their own, 4 bytes each. Their values are written `Color.red` and are numbered from 0 unless given a value. `c as int` and `n as Color` convert.
- **Functions** without a result leave out the type. Parameters are local variables holding copies of the arguments. A function with a result must end every path with `return`.

`extern fn` declares a function written in assembly, which the linker supplies under exactly that name:

```
extern fn clear_screen();
```

`interrupt fn` declares an interrupt handler. It returns nothing and takes either nothing or one pointer. The CPU saves every register when it enters a handler and restores them on `IRET`, so the handler may use any of them. The pointer points at the saved registers, R0 first, as [`cpu.Frame`](#stdcpu) lays them out, and what the handler writes there is what the interrupted code gets back. A handler cannot be called; `handler as u32` is its address for the interrupt vector table.

```
interrupt fn on_call(frame: *cpu.Frame) {
    frame.r0 = frame.r5 * 2;        // the result of an INT that passed its argument in R5
}
```

## Statements

```
var x: int = 5;                     // local variable
var y = x * 2;                      // type taken from the value
var buffer: [64]u8;                 // zero until assigned
var key: u8 = 'p';
var paused = false;

x = 3;
x += 1;                             // also -= *= /= %= &= |= ^= <<= >>=

if (x < 0) {
    x = -x;
} else if (x == 0) {
    x = 1;
} else {
    x -= 1;
}

while (x > 0) {
    x -= 1;
}

for (var i = 0; i < buffer.len; i += 1) {
    buffer[i] = 'a';
}

switch (key) {
    case 'q', 'Q' {
        return 0;
    }
    case 'p' {
        paused = !paused;
    }
    default {
        x = 0;
    }
}
```

- The bodies of `if`, `while`, `for` and `switch` cases are always blocks.
- Assignments are statements, not expressions. The only expressions that can stand alone as statements are calls.
- A `switch` compares an integer or enum with constant cases, and runs exactly one block. There is no fallthrough.
- `break` and `continue` apply to the innermost `while` or `for`. A `switch` is not a loop.
- Blocks can declare constants as well as variables.
- A name is visible from its declaration to the end of its block. A local name may hide a module-level one, but not another local or parameter.

## Expressions

Operators from the highest precedence to the lowest:

| Operators | |
|---|---|
| `f(x)` `a[i]` `a[i:j]` `x.name` | Call, index, slice, field |
| `-` `!` `~` `*` `&` | Negate, not, complement, dereference, address of |
| `as` | Conversion |
| `*` `/` `%` | |
| `+` `-` | |
| `<<` `>>` | |
| `&` | |
| `^` | |
| <code>&#124;</code> | |
| `==` `!=` `<` `<=` `>` `>=` | Cannot be chained |
| `&&` | |
| <code>&#124;&#124;</code> | |

Unlike C, `&`, `^` and `|` bind tighter than comparisons, so `flags & MASK == 0` means `(flags & MASK) == 0`.

- **Arithmetic** needs two operands of the same integer type, or two `f32`s, and gives that type; `%` is only for integers. Integers wrap around on overflow. Signed `/` rounds toward zero and `%` takes the sign of the dividend. `f32` arithmetic is IEEE single precision: dividing by zero gives an infinity or NaN, and a comparison with NaN is false, except for `!=`.
- **Shifts** take an integer type on the left and any integer type on the right. `>>` copies the sign bit into signed values and zeros into unsigned ones. Only the low five bits of the count are used, as the CPU does.
- **Comparisons** need two operands of the same type. Booleans, enums and function pointers only compare with `==` and `!=`. Slices, arrays and structs do not compare; the standard library compares their contents.
- **`&&` and `||`** take booleans and skip their right side when the left one decides.
- **Conditions** of `if`, `while` and `for` must be `bool`: write `n != 0` and `p != null`.
- **Field access** looks through one pointer: `p.x` works for a `Point` and for a `*Point`.
- **Order:** operands and arguments are evaluated from left to right.

`Point { x: 1, y: 2 }` builds a struct, and any field left out is zero. `[3]int { 1, 2, 3 }` builds an array, and `[]int { 1, 2, 3 }` builds an array and gives a slice of it.

### Constants without a type

Integer literals, character literals and constants declared without a type have no type of their own. They are computed exactly, and turn into whatever integer type the context needs, as long as their value fits: `var b: u8 = 200;` works, `var b: u8 = 300;` does not compile. Where an `f32` is needed they turn into the nearest one, so `x * 2` works for an `f32` x. Without a context they become `int`. Float literals are `f32` constants, and constants computed from them are rounded after each operation, as at run time.

### Conversions

Where a value meets another type, in an assignment, an argument, a result or next to another operand, it converts by itself only where no information can be lost:

| From | To |
|---|---|
| An integer type | A larger integer type that holds all of its values: `u8` to `u16`, `u32`, `i16` or `int` |
| `i8`, `u8`, `i16`, `u16` | `f32` |
| `[N]T` | `[]T` covering the whole array |
| `null` | Any pointer or function type |

Everything else takes `as`:

| `x as T` | |
|---|---|
| Integer to integer | Keeps the low bits, or extends by the sign of the source |
| Integer to `f32` and back | Rounds to the nearest float; back to an integer rounds toward zero, and NaN or a float outside the integer type's range gives an unspecified number |
| Integer to pointer and back, pointer to pointer | Keeps the address |
| Function to `u32` | The function's address |
| `bool` to integer | 1 or 0 |
| Enum to integer and back | The enum's number |

`int` and `u32` do not mix without `as`, since each has values the other lacks. Lengths and indexes are `int`.

## Modules

Every file is a module, named after the file. `import` makes another module's `pub` declarations available through its name:

```
// geometry.ore
pub struct Point {
    x: int;
    y: int;
}

pub fn origin() Point {
    return Point {};
}
```

```
// main.ore
import "geometry";
import "std/str" as s;

fn main() int {
    var p = geometry.origin();
    if (s.equal("a", "b")) {
        return 1;
    }
    return p.x;
}
```

- The path names the file without `.ore`, relative to the importing file, or else in the directories given with `-I` and the standard library.
- `as` gives the module another name in this file.
- Modules may import each other in circles, since the compiler reads the whole program before it compiles any of it.
- A module can bring assembly for its `extern fn` functions: a file with the module's name and `.asm`, next to it, becomes part of every program that imports the module.

## Memory

- Global variables live in the data section. Local variables live on the stack, which holds 64 KB unless `vm -S` gives it another size.
- `new(T)` allocates a zeroed `T` on the heap and returns a `*T`. `new(T, n)` allocates *n* of them and returns a `[]T`. `free(x)` gives either back, and does nothing for `null`.
- Nothing is freed automatically.

The VM checks every heap access, so reading or writing freed memory, or past the end of an allocation, stops the program.

### Runtime errors

These stop the program with a message, the source line and the calls that led there.

- An index or a slice outside the bounds of its array or slice
- Using a `null` pointer or calling a `null` function
- Division by zero, and dividing the most negative integer by −1
- `new` finding no room, and `free` of memory that `new` did not return or that is already free
- Stack overflow, and the heap misuse above
- `panic` and a failed `assert`

```
fn digit(n: int) u8 {
    return "0123456789"[n];
}

fn main() {
    print(digit(10));
}
```

```console
$ ./vm digits.bin
vm: error: index 10 is out of bounds for length 10
vm: at 0x0040 <digits.digit+16> digits.ore:2: CALL rt.index_error
vm: #1 0x0084 <digits.main+12> digits.ore:6: CALL digits.digit
```

Integer overflow is not an error. It wraps around.

## Builtins

| Builtin | |
|---|---|
| `print(a, b, ...)` | Writes each argument to stdout: integers in decimal, booleans as `true` or `false`, `[]u8` and arrays of `u8` as text, `f32` with up to six digits, pointers in hex |
| `new(T)`, `new(T, n)` | Allocate zeroed memory (see [Memory](#memory)) |
| `free(x)` | Free a pointer or slice from `new` |
| `sizeof(T)` | The size of `T` in bytes, a constant |
| `panic(message)` | Stop with a runtime error |
| `assert(condition)` | Stop with a runtime error if the condition is false |
| `syscall(n, a, b, c)` | Make syscall *n*, a constant, with R0, R5 and R6 set to `a`, `b` and `c`, all optional: integers, floats, pointers, booleans or enums. Returns a `SyscallResult` with fields `value` (R0) and `status` (R5) |

A `u8` prints as a number; `print("a")` prints a character.

## Programs

A program is the module given to the compiler and every module it imports. Its `main` function takes no parameters or `args: [][]u8`, which holds the program path and its arguments. `main` returns nothing, or an `int` that becomes the exit status. Nothing runs before `main`.

## Standard library

The standard library lives in `ore/lib/std` and is imported as `std/io`, `std/str` and so on. It is written in Ore, apart from `std/cpu` and `std/math`, which are assembly.

### std/io

| Function | |
|---|---|
| `write(handle: int, bytes: []u8) int` | Writes bytes and returns how many, or −1 |
| `read(handle: int, buffer: []u8) int` | Reads into buffer and returns how many bytes, 0 at the end, or −1 |
| `read_line(buffer: []u8, line: *[]u8) bool` | Reads a line of stdin without its line end, and points `line` at it inside buffer. Returns false at the end of the input. A line longer than the buffer comes in pieces |
| `read_char() int` | The next byte of stdin, or −1 at the end |
| `print_error(text: []u8)` | Writes to stderr |
| `print_char(c: u8)`, `print_hex(value: u32)` | Writes a byte, or a number like `0x1f`, to stdout |
| `open(path: []u8, mode: int) int` | A handle for the file, or −1. The modes are `READ`, `WRITE` (creating or emptying the file), `APPEND`, `UPDATE` (reading and writing a file that exists) and `REPLACE` (reading and writing a file it creates or empties) |
| `close(handle: int)` | |
| `seek(handle: int, offset: int, origin: int) int` | Moves to offset from `START`, `CURRENT` or `END`, and returns the new position or −1 |
| `read_file(path: []u8) []u8` | The whole file on the heap, or a slice whose `ptr` is `null` if it cannot be read |
| `write_file(path: []u8, bytes: []u8) bool` | Creates or replaces the file |

`STDIN`, `STDOUT` and `STDERR` are the handles 0, 1 and 2. Paths are relative to the directory `vm` runs in.

### std/str

| Function | |
|---|---|
| `equal(a: []u8, b: []u8) bool`, `compare(a: []u8, b: []u8) int` | Compare byte by byte; `compare` is negative, zero or positive |
| `starts_with(text, prefix) bool`, `ends_with(text, suffix) bool` | |
| `find(text, part) int`, `find_byte(text, c) int`, `find_last_byte(text, c) int` | The index, or −1 |
| `trim(text) []u8` | Without spaces, tabs and line ends at either end |
| `is_space(c)`, `is_digit(c)`, `is_letter(c)` | |
| `clone(text) []u8` | A copy on the heap |
| `parse_int(text) Number` | A decimal number, or a hexadecimal one after `0x`, with an optional `-`. `Number` has `value` and `ok`, which is false for anything else or a value that does not fit in an `int` |
| `format_int(value: int, buffer: []u8) []u8`, `format_hex(value: u32, buffer: []u8) []u8` | Write the digits at the end of buffer and return them; 11 bytes are always enough |
| `parse_float(text) Float` | A decimal number with an optional `-`, fraction and exponent, as in `12.5`, `.5`, `-2e-3` or `6.02E23`, or `inf` or `nan`, rounded to the nearest `f32`. `Float` has `value` and `ok`, which is false for anything else or a number too large for an `f32` |
| `format_float(value: f32, buffer: []u8) []u8` | Writes value as `print` shows it at the start of buffer and returns that part; 12 bytes are always enough. Both round exactly, halfway cases to the even digit |

A `Builder` collects text on the heap:

| Function | |
|---|---|
| `builder(capacity: int) Builder` | An empty builder |
| `append(b: *Builder, text: []u8)`, `append_byte`, `append_int`, `append_hex`, `append_float` | Add to the end |
| `reserve(b: *Builder, count: int)` | Makes room for count more bytes |
| `text(b: *Builder) []u8` | What was built so far; it moves when the builder grows |
| `clear(b: *Builder)`, `release(b: *Builder)` | Empty it, or give its memory back |

### std/mem

| Function | |
|---|---|
| `copy(to: []u8, from: []u8) int` | Copies as many bytes as both hold, even when they overlap, and returns how many |
| `fill(to: []u8, value: u8)` | |
| `equal(a, b) bool`, `compare(a, b) int` | As in `std/str` |
| `bytes(address: *u8, size: int) []u8` | The bytes of any memory, such as `mem.bytes(&p as *u8, sizeof(Point))` |

### std/sys

| Function | |
|---|---|
| `exit(status: int)` | Stops the program |
| `time() int` | Milliseconds since the program started |
| `ticks() u32` | Instructions executed so far |
| `sleep(milliseconds: int)` | |
| `random(limit: u32) u32`, `seed(value: u32)` | A number below limit, or any 32-bit value for 0. The sequence repeats unless seeded |
| `memory_size() int` | The size of the VM's memory in bytes |

### std/math

| Function | |
|---|---|
| `sqrt(x: f32) f32` | The square root, NaN for a negative x |
| `abs(x: f32) f32` | x without its sign |

Each is one instruction, `FSQRT` or `FABS`, behind a call.

### std/cpu

These need supervisor mode, which programs start in.

| Function | |
|---|---|
| `read_port(port: int) u32`, `write_port(port: int, value: u32)` | [I/O ports](../README.md#io-ports) |
| `enable_interrupts()`, `disable_interrupts()` | Start or stop the delivery of device interrupts |
| `set_handler(vector: int, handler: u32)` | Makes `handler`, the address of an `interrupt fn` (`on_tick as u32`), the handler of vector. The first call sets up a vector table if there is none |
| `read_control(register: int) u32`, `write_control(register: int, value: u32)` | Control registers, numbered by the constants `IVTB`, `KSP`, `PTB`, `FADDR`, `ECODE`, `SLO`, `SHI`, `HEAPLO` and `HEAPHI` |
| `halt()` | Stops the machine |
| `enter_user(pc: u32, sp: u32) u32` | Runs code in user mode from pc, with its stack at sp and interrupts off, until a handler of an interrupt from it calls `leave_user`, and returns the value given there. Interrupts from the code use the stack below the caller's, so sp has to lie below that and within `SLO` and `SHI`. Handlers may call it again |
| `leave_user(value: u32)` | Ends the code `enter_user` started. Only a handler of an interrupt from that code may call it |
| `Frame` | The registers an interrupt saved: `r0`, `bp`, `sp`, `pc`, `sr`, `r5` to `r14` and `lr`, each a `u32` |

## Not in Ore

- Generics, methods, closures, exceptions and garbage collection
- Variadic functions (`print` is built in), macros and a preprocessor
- Inline assembly (`extern fn` calls assembly instead)
- `++`, `--` and assignments inside expressions
- Unions, bit fields, `goto` and `defer`
- Implicit conversions between integers and `bool`

## Implementation

`vmc`, written in Ore in `ore/compiler`, and `vmc0`, the bootstrap compiler written in C, read the main module and its imports and write the whole program as one assembly file with `.loc` lines. It holds only the functions and globals that `main` reaches, through calls, function values and the initial values of globals. It includes the runtime and any assembly files given on the command line, and is then assembled with `vmasm`. The two write different code for the same program, since only `vmc` optimizes, but the code behaves the same. `-S` stops after the assembly, `-o` names the output and `-g0` leaves out the debug information: the `.loc` lines, and the symbols and source lines of the binary. `vmc` runs on the VM, where it keeps constants exact to 64 bits in a pair of 32-bit halves and rounds float literals with [`str.parse_float`](#stdstr), whose exact arithmetic gives the same `f32`s as `vmc0` gets from C's `strtof`.

**Calling convention:**
- **Arguments** are pushed from right to left, each taking its size rounded up to 4 bytes, and the caller removes them.
- **Frames** use `ENTER` and `LEAVE`: arguments start at `[BP+8]` and locals lie below BP. `vmc` gives a function that keeps no locals in memory no frame; it reaches its arguments through SP instead.
- **Results:** integers, booleans, enums and pointers return in R0, and slices in R0 (the pointer) and R5 (the length). For a struct or array, the caller passes the address of room for it as a hidden first argument, and the callee returns that address in R0.
- **Registers:** R0 and R5–R7 are free for the callee to change; R8–R15 must come back unchanged.

**Symbols:** functions and globals of module `m` are named `m.name` in assembly, and `main` calls the main module's `main`. The program's assembly files may name them too, which keeps them in the program even when no Ore code uses them. `extern fn` names stay as written. An `interrupt fn` ends with `IRET` instead of `RET`.

**Runtime:** `ore/lib/runtime.asm` calls `main` and passes its result to the Exit syscall. It is a [library](../README.md#libraries), so a program only gets the routines and messages that it can use. For a runtime error it formats the message and makes syscall 35, Abort, which stops the program the way a fault does. R0 holds the message, and R5 the number of calls to leave out of the report, so that it starts at the Ore line that failed.
