# MiniDos

MiniDos is a small DOS for VM32, written in [Ore](language.md). It keeps its files on the VM's [disk](../README.md#disk) in a file system of its own, reads commands at an `A:\>` prompt, runs batch files, redirects input and output into files and through pipes, and loads programs from its disk and runs them in user mode. Programs are ordinary VM32 binaries: anything `vmc` or `vmasm` builds runs under MiniDos unchanged, and its syscalls reach MiniDos instead of the VM.

```console
$ make
$ ./vm -m 4096 -b minidos.img minidos.bin

Starting MiniDos...

A:\>ECHO pear > FRUIT.TXT
A:\>ECHO apple >> FRUIT.TXT
A:\>SORT < FRUIT.TXT
apple
pear
A:\>DIR | FIND "TXT"
FRUIT    TXT            11
```

`make` builds `minidos.bin` and `minidos.img`, a 4 MB disk that MiniDos formatted itself and filled with the programs in `ore/minidos/programs`, `SORT.EXE` and `FIND.EXE`. Another disk takes `dd if=/dev/zero of=disk.img bs=512 count=8192` and `FORMAT`. MiniDos needs 2 MB of memory or more, so `vm` has to be given `-m`.

- **Starting:** MiniDos runs `\AUTOEXEC.BAT` when the disk has one. `-e` after `minidos.bin` shows each command after the prompt, for sessions that come from a file.
- **Stopping:** `EXIT` or the end of the input stops MiniDos and the VM.
- **Host files:** `IMPORT` and `EXPORT` copy files between the disk and the directory `vm` runs in.

## Commands

Names of commands, files and directories may be written in any case. Paths take `\` or `/`, may start with `A:` and may contain `.` and `..`.

| Command | |
|---|---|
| `CD` or `CHDIR` [*dir*] | Shows or changes the current directory. `CD..` and `CD\` work too |
| `CLS` | Clears the screen |
| `COPY` *from* [*to*] | Copies files, which *from* may name with wildcards, into a file or a directory. `COPY CON` *file* makes a file of the lines that follow, up to a line with Ctrl-Z |
| `DEL` or `ERASE` *files* | Deletes files, named with wildcards or as a directory for all of its files |
| `DIR` [*files*] [`/W`] | Lists files with their sizes, or only their names five to a line with `/W` |
| `ECHO` [*text*] | Shows text, `ECHO.` an empty line. `ECHO ON` and `ECHO OFF` decide whether batch files show their commands |
| `EXIT` | Stops MiniDos |
| `EXPORT` *file* *host* | Copies a file to the host |
| `FORMAT` [*label*] | Makes an empty file system of the whole disk, without asking |
| `HELP` | Lists the commands |
| `IMPORT` *host* [*file*] | Copies a file from the host, keeping its name unless *file* gives another |
| `MD` or `MKDIR` *dir* | Makes a directory |
| `MEM` | Shows the memory for programs, all memory and the free heap |
| `PATH` [*dirs*] | Shows or sets the directories, separated by `;`, where programs are looked for after the current one. `PATH ;` clears it |
| `PAUSE` | Waits for Enter |
| `RD` or `RMDIR` *dir* | Removes an empty directory |
| `REM` [*text*] | Does nothing |
| `REN` or `RENAME` *file* *name* | Renames a file or directory in its directory |
| `SET` [*name*`=`[*value*]] | Shows, sets or, without a value, removes environment variables |
| `TYPE` *file* | Shows a file |
| `VER` | Shows the version |
| `VOL` | Shows the disk's label |

Any other name runs a program or a batch file: `NAME` looks for `NAME.EXE`, then `NAME.BAT`, in the current directory and then in those of `PATH`. The words after it become the program's arguments, where double quotes keep a word with spaces together.

**Redirection:** `> file` sends a command's output into a file, `>> file` adds it to the end, and `< file` gives a program the file as its input. `a | b` runs `a` with its output going to a temporary file in the root directory, then `b` with that file as its input. Error messages always go to the screen.

**Wildcards:** `*` stands for the rest of the name or extension and `?` for one character, as in `DEL *.TXT` or `DIR A?C.*`. `DIR NAME` lists `NAME.*`.

## Batch files

A batch file, `NAME.BAT`, runs its lines as commands. While `ECHO` is on it shows each one after the prompt; a line that starts with `@` is not shown.

- `%0` is the batch file's name and `%1` to `%9` are its arguments. `SHIFT` moves them down by one.
- `%NAME%` is the value of an environment variable, and `%%` a single `%`.
- `:LABEL` marks a line that `GOTO LABEL` continues after.
- `IF [NOT] EXIST file command`, `IF [NOT] ERRORLEVEL n command` and `IF [NOT] one==two command` run the command when the condition holds. `ERRORLEVEL n` holds when the last program ended with status *n* or more.
- A batch file that runs another continues after it when it ends.

## Programs

MiniDos loads a program's code and data where its header puts them, below `0x100000`, and starts it in user mode with its stack at `0x100000`, growing down towards its data. MiniDos itself lives above, built with `vmc -b 0x100000`. Its heap, which programs share, follows it, and its own stack is at the top of memory.

A program's syscalls cannot reach the VM from user mode; they arrive at MiniDos as privilege violations, and MiniDos does what the VM would:

| Syscalls | Under MiniDos |
|---|---|
| Console, 0 to 9 | Go to the screen or where the command's output is redirected, and read the keyboard or the redirected input |
| Files, 10 to 14 | Work with files on the MiniDos disk. Handles 0, 1 and 2 are the input, the output and the screen, and up to 16 files may be open |
| Heap, 20 to 23 | Allocate from the VM's heap. What a program does not free, MiniDos frees when it ends, along with the files it leaves open |
| 30, Exit | Ends the program with a status, which becomes `ERRORLEVEL` |
| 34, Argument | Gives the program's path, as `A:\NAME.EXE`, and its arguments |
| 35, Abort | Shows the message of a runtime error, such as a `null` pointer in Ore, and ends the program with status 1 |
| Time and random numbers, 31 to 33, 40 and 41 | Go to the VM |

A fault ends the program with a message, such as `Divide overflow` or `Stack overflow`, and status 255; so does any other privileged instruction. `HALT` ends it with status 0. Memory is not protected, as under DOS: a program that writes outside its own memory can damage MiniDos.

`ore/minidos/programs` holds the programs `make` puts on `minidos.img`:

- `SORT [/R] < file` writes the lines of its input in order, ignoring case, or in reverse order with `/R`.
- `FIND [/V] [/C] [/I] "text" [file]` writes the lines that contain text, with `/V` those that do not, with `/C` only how many, and with `/I` ignoring case. It ends with status 1 when no line matched.

A new program is any Ore program that reads and writes with `std/io`:

```console
$ ./vmc hello.ore
$ ./vm -m 4096 -b minidos.img minidos.bin
A:\>IMPORT hello.bin HELLO.EXE
A:\>HELLO
```

## The file system

Sector 0 describes the disk, the allocation table follows it, and the rest of the disk is clusters of one sector each.

| Sector 0, from offset | |
|---|---|
| 0 | `MDOS` |
| 4 | Version, 1 |
| 8 | Sectors on the disk |
| 12 | First sector of the allocation table |
| 16 | Its number of sectors |
| 20 | Sector of cluster 1 |
| 24 | Number of clusters |
| 28 | First cluster of the root directory |
| 32 | Label, 11 characters filled up with spaces |

The allocation table has a 32-bit entry for every cluster, starting with an unused one for cluster 0: 0 for a free cluster, -1 for the last cluster of a file, or the number of the file's next cluster. Every value is little-endian.

Directories are files of 32-byte entries, the root directory included. Every other directory starts with `.` and `..`, which name itself and its parent.

| Entry, from offset | |
|---|---|
| 0 | Name, 8 characters and 3 of extension in capitals, filled up with spaces; a free entry starts with 0 |
| 11 | Attributes: 0x10 for a directory |
| 12 | First cluster, 0 for an empty file |
| 16 | Size in bytes |
| 20 | Reserved, 12 bytes |

## Source

| File | |
|---|---|
| `ore/minidos/main.ore` | Starting up |
| `ore/minidos/shell.ore` | Commands, batch files, redirection and pipes |
| `ore/minidos/program.ore` | Loading programs, serving their syscalls and ending them after faults |
| `ore/minidos/fs.ore` | The file system |
| `ore/minidos/disk.ore` | The disk's ports |
| `ore/minidos/console.ore` | Output and input, redirected or not |
| `ore/minidos/machine.ore` | Two things Ore cannot reach itself, in `machine.asm` |
| `ore/minidos/programs/` | `SORT` and `FIND` |

`tests/minidos` holds MiniDos sessions for the test suite: what is typed in `NAME.in`, what the screen shows in `NAME.out`, and the programs the sessions import.

## Limits

- One disk, `A:`, and no dates or times, as the VM has no clock.
- One program at a time, with 1 MB for its code, data and stack.
- No memory protection, and no interrupts: programs cannot be stopped from outside.
