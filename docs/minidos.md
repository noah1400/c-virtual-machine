# MiniDos

MiniDos is a small DOS for VM32, written entirely in [Ore](language.md). It keeps its files in a file system of its own on up to four of the VM's [disks](../README.md#disk), reads commands at an `A:\>` prompt with a line editor, runs batch files, redirects input and output into files and through pipes, edits text on the whole screen, and loads programs from its disks and runs them in user mode. Programs are ordinary VM32 binaries: anything `vmc` or `vmasm` builds runs under MiniDos unchanged, and its syscalls reach MiniDos instead of the VM.

## Quick start

```console
$ make
$ ./vm -b work.img minidos.bin

Starting MiniDos...

The disk in drive A is new and not formatted yet.
Format it now (Y/N)?Y
Size of the new disk in K, or in M with an M after it (ENTER for 4M)?
Formatting 4,096K
Format complete.
System transferred

Volume label (11 characters, ENTER for none)? WORK

   4,161,024 bytes total disk space
   4,147,200 bytes available on disk

         512 bytes in each allocation unit
       8,100 allocation units available on disk

A:\>DIR

 Volume in drive A is WORK
 Directory of A:\

DOS             <DIR>      10-01-26   9:30a
AUTOEXEC BAT            22 10-01-26   9:30a
        1 file(s)             22 bytes
        1 dir(s)       4,147,200 bytes free
```

`vm` creates `work.img` when it does not exist, as a disk without sectors, and MiniDos offers to format it. Formatting makes the disk as large as asked, puts the system programs `SORT` and `FIND` into `\DOS` and writes an `AUTOEXEC.BAT` that adds `\DOS` to `PATH`. The next start finds the formatted disk and goes straight to the prompt.

`make` also builds `minidos.img`, a 4 MB disk formatted the same way, labelled `MINIDOS`. It builds that disk anew whenever MiniDos changes, so files of your own belong on a disk like `work.img`, where `SYS A:` brings the system programs up to date. MiniDos asks for 4 MB of memory in its binary's header, which `vm` gives it without `-m`.

- **More drives:** each further `-b` attaches the next drive, up to `D:`, as in `./vm -b work.img -b data.img minidos.bin`. MiniDos says which disks are not formatted yet, and `FORMAT B:` prepares one.
- **Host files:** `IMPORT` and `EXPORT` copy files between a MiniDos disk and the directory `vm` runs in.
- **Stopping:** `EXIT`, or the end of the input when it comes from a file or a pipe, stops MiniDos and the VM. Ctrl-C stops the VM at once.
- **The clock:** files get the local date and time of the host when they are written. `DATE` and `TIME` move MiniDos's clock until it stops, without touching the host's, and `vm -T 2026-10-01T09:30:00` holds the clock still.

## The command line

Commands are typed into a line editor:

| Key | |
|---|---|
| Left, Right, Home, End | Move through the line |
| Backspace, Delete | Delete the character before or under the cursor |
| Up, Down | Bring back the last 32 commands |
| Tab | Completes the file or directory name before the cursor, and offers the next match with each further Tab |
| Escape | Clears the line |
| Ctrl-Z | Ends the input of `COPY CON` or a program after this line; it shows as `^Z` until Enter |

Names of commands, files and directories may be written in any case and have up to 8 characters with an extension of up to 3. Paths take `\` or `/`, may start with a drive such as `B:` and may contain `.` and `..`. Typing a drive alone, as in `B:`, makes it the current drive, and each drive keeps a current directory of its own.

Every command shows its help after `/?`, and `HELP` lists them all. Switches may follow the parameters or come before them.

Any other name runs a program or a batch file: `NAME` looks for `NAME.EXE` and then `NAME.BAT`, in the current directory and then in the directories of `PATH`. The words after it become the program's arguments, where double quotes keep a word with spaces together.

- **Redirection:** `> file` sends a command's output into a file, `>> file` adds it to the end, and `< file` gives the command the file as its input. Error messages and questions always go to the screen.
- **Pipes:** `a | b` runs `a` with its output going to a temporary file in the root directory, then `b` with that file as its input.
- **Wildcards:** `*` stands for the rest of the name or extension and `?` for one character, as in `DEL *.TXT` or `DIR A?C.*`. `DIR NAME` lists `NAME.*`.
- **NUL:** every directory has the device `NUL`, with any extension, which reads as empty and takes whatever is written to it: `> NUL` throws output away, `COPY NUL FILE` makes an empty file, and `IF EXIST DIR\NUL` tells whether a directory exists.

## Commands

**Files and directories**

| Command | |
|---|---|
| `ATTRIB` [`+R`\|`-R`] [`+H`\|`-H`] [`+S`\|`-S`] [`+A`\|`-A`] [*files*] [`/S`] | Shows or changes attributes: read-only, hidden, system and archive. `/S` works through the subdirectories too |
| `CD` or `CHDIR` [*drive*`:`][*path*] | Shows or changes the current directory. `CD..` and `CD\` work too |
| `COPY` [`/Y`\|`/-Y`] *source* [`+` *source* ...] [*destination*] | Copies files, which *source* may name with wildcards, into a file, a directory or a drive, or joins sources into one file. `COPY CON` *file* makes a file of the lines typed next, up to Ctrl-Z. It asks before replacing a file, except in batch files or with `/Y`. Copies keep the time of their source |
| `DEL` or `ERASE` *files* [`/P`] | Deletes files, named with wildcards or as a directory for all its files, which takes a confirmation. `/P` asks for each file. Read-only files stay |
| `DELTREE` [`/Y`] *directory* ... | Deletes directories with everything in them, after asking unless `/Y` |
| `DIR` [*files*] [`/P`] [`/W`] [`/A`[`:`*attributes*]] [`/O`[`:`*order*]] [`/S`] [`/B`] | Lists files with their sizes and times. `/P` waits after each screenful, `/W` shows five names to a line, `/B` only the names. `/A` adds hidden files, or with `D`, `H`, `R`, `S`, `A` shows only those, `-` leaving them out. `/O` sorts by `N` name, `E` extension, `S` size, `D` date or `G` directories first, `-` reversing. `/S` lists the subdirectories as well |
| `EDIT` [*file*] | Edits a text file on the whole screen; see [EDIT](#edit) |
| `MD` or `MKDIR` *path* | Makes a directory |
| `MORE` [*file*] | Shows a file or its input a screenful at a time, as in `TYPE LONG.TXT \| MORE` |
| `MOVE` [`/Y`\|`/-Y`] *files* *destination* | Moves files to another directory or drive, or renames a directory |
| `RD` or `RMDIR` *path* | Removes an empty directory |
| `REN` or `RENAME` *files* *name* | Renames files or a directory. Wildcards in *name* keep the old letters, as in `REN *.TXT *.BAK` |
| `TREE` [*path*] [`/F`] | Draws the directories as a tree, with their files after `/F` |
| `TYPE` *file* ... | Shows files |
| `XCOPY` *source* [*destination*] [`/S`] [`/E`] [`/Y`\|`/-Y`] | Copies files, with `/S` the subdirectories that hold files too, and with `/E` the empty ones as well |

**Disks**

| Command | |
|---|---|
| `CHKDSK` [*drive*`:`] [`/F`] | Checks the file system for lost clusters, cross-linked files and wrong sizes, and shows how the disk is used. `/F` frees lost clusters and corrects sizes |
| `DISKCOPY` *drive*`:` *drive*`:` | Copies a whole disk onto another, which takes its size |
| `FORMAT` *drive*`:` [`/V:`*label*] [`/F:`*size*] [`/S`] | Makes an empty file system; see [Disks](#disks) |
| `LABEL` [*drive*`:`][*label*] | Shows, changes or removes a disk's label |
| `SYS` *drive*`:` | Puts the system programs onto a formatted disk |
| `VOL` [*drive*`:`] | Shows a disk's label |

**The shell and batch files**

| Command | |
|---|---|
| `CALL` *batch* [*parameters*] | Runs a batch file from another one and comes back |
| `CHOICE` [`/C:`*keys*] [`/N`] [`/S`] [*text*] | Waits for one of the keys, `YN` unless given, and sets `ERRORLEVEL` to its place. `/N` leaves the keys out of the question and `/S` tells capitals from small letters |
| `ECHO` [`ON`\|`OFF`\|*text*] | Shows text, `ECHO.` an empty line. `ECHO ON` and `ECHO OFF` decide whether batch files show their commands |
| `EXIT` | Stops MiniDos and the VM |
| `FOR %`*v* `IN (`*set*`) DO` *command* | Runs the command for each word of the set, and for each file that a word with wildcards names, with `%`*v* standing for it. Batch files write `%%`*v* |
| `GOTO` *label* | Continues a batch file after the line `:`*label* |
| `HELP` [*command*] | Lists the commands, or tells about one |
| `IF` [`NOT`] `EXIST` *file* *command* | Runs the command when files exist, wildcards allowed |
| `IF` [`NOT`] `ERRORLEVEL` *n* *command* | Runs the command when the last program ended with status *n* or more |
| `IF` [`NOT`] *text*`==`*text* *command* | Runs the command when the texts are equal |
| `PATH` [*path*[`;`...]] | Shows or sets where programs are looked for after the current directory. `PATH ;` clears it |
| `PAUSE` | Waits for Enter |
| `PROMPT` [*text*] | Changes the prompt: `$P` drive and directory, `$N` drive, `$D` date, `$T` time, `$V` version, `$G` `>`, `$L` `<`, `$B` `\|`, `$Q` `=`, `$$` `$`, `$_` a new line, `$E` escape, `$H` backspace. `PROMPT` alone brings back `$P$G` |
| `REM` [*text*] | Does nothing |
| `SET` [*name*`=`[*value*]] | Shows, sets or, without a value, removes environment variables |
| `SHIFT` | Moves the parameters of a batch file down by one |

**The system**

| Command | |
|---|---|
| `CLS` | Clears the screen |
| `DATE` [*date* \| `/T`] | Shows the date and asks for a new one, as *mm*`-`*dd*`-`*yy* or *mm*`-`*dd*`-`*yyyy* from 1980 to 2099; Enter keeps it. `/T` only shows it |
| `EXPORT` *file* *host-file* | Copies a file to the host |
| `IMPORT` *host-file* [*file*] | Copies a file from the host, keeping its name unless *file* gives another |
| `MEM` | Shows the memory for programs, all memory and the free heap |
| `TIME` [*time* \| `/T`] | Shows the time and asks for a new one, as *hh*`:`*mm*[`:`*ss*[`.`*xx*]] with `a` or `p` after it for the 12-hour clock |
| `VER` | Shows the version |

## Disks

A disk is an image file of 512-byte sectors that `vm -b` attaches; a missing one is created without sectors. MiniDos formats disks of 8 KB to 32 MB.

`FORMAT` *drive*`:` asks for the size of a disk that has no sectors yet, 4 MB unless another is typed, as in `512K` or `16M`; `/F:`*size* gives it at once and also resizes a disk that has a size. It warns and asks before it formats a disk that holds a file system, asks for a label unless `/V:`*label* gives one, and with `/S` puts the system programs on the new disk. `/Q` and `/U` are accepted out of habit: every format only writes the file system's first sectors.

The system programs live in `\DOS`, and `AUTOEXEC.BAT` in the root directory, which MiniDos runs when it starts from drive A, puts `\DOS` on `PATH`. `SYS` adds them to a disk that is formatted already and keeps an `AUTOEXEC.BAT` that is there.

## EDIT

`EDIT` *file* opens a file on the [display](../README.md#display), or a new one when the file does not exist; `EDIT` alone starts without a name. The top line shows the file's path and whether it changed, and the bottom line the keys, messages, questions and the cursor's line and column. When the editor ends, the terminal shows again what it showed before.

| Key | |
|---|---|
| Arrows, Home, End | Move the cursor; Up and Down keep to the column |
| Page Up, Page Down | Move a screenful |
| Enter, Backspace, Delete | Split and join lines as well as delete |
| Insert | Switches between inserting and overwriting, `OVR` in the bottom line |
| Tab | Inserts a tab, which reaches to the next column of 8 |
| ^S | Saves, after asking for a name when the text has none |
| ^Q or Escape | Quits, after asking whether to save changes; Escape goes back to the text |
| ^F | Finds text after the cursor, whatever its case, going on from the top; Enter on the last text finds the next |
| ^G | Goes to a line by its number |
| ^K | Cuts the cursor's line; cutting lines one after another gathers them |
| ^U | Puts the lines cut last before the cursor's line |

Lines longer than the screen scroll sideways. Saving ends every line, apart from a last empty one, with the line end the file had, `CR LF` or `LF`, which is `LF` for a new file.

## Batch files

A batch file, `NAME.BAT`, runs its lines as commands. While `ECHO` is on it shows each one after the prompt; a line that starts with `@` is not shown.

- `%0` is the batch file's name and `%1` to `%9` are its parameters. `SHIFT` moves them down by one.
- `%NAME%` is the value of an environment variable, and `%%` a single `%`.
- `:LABEL` marks a line that `GOTO LABEL` continues after.
- `IF`, `FOR`, `CHOICE` and `PAUSE` make decisions and wait for answers, and `ERRORLEVEL` holds the status of the last program or `CHOICE`.
- A batch file that runs another one without `CALL` hands over to it, as under DOS; `CALL` comes back.
- Commands in batch files replace files without asking.

## Programs

MiniDos loads a program's code and data where its header puts them, below `0x100000`, and starts it in user mode with its stack at `0x100000`, growing down towards its data. MiniDos itself lives above, built with `vmc -b 0x100000`. Its heap, which programs share, follows it, and its own stack is at the top of memory.

A program's syscalls cannot reach the VM from user mode; they arrive at MiniDos as privilege violations, and MiniDos does what the VM would:

| Syscalls | Under MiniDos |
|---|---|
| Console, 0 to 9 | Go to the screen or where the command's output is redirected, and read the keyboard through the line editor or the redirected input |
| Files, 10 to 14 | Work with files on the MiniDos disks. Handles 0, 1 and 2 are the input, the output and the screen, and up to 16 files may be open |
| Memory, 20 to 23 | Allocate from the VM's heap. What a program does not free, MiniDos frees when it ends, along with the files it leaves open |
| 30, Exit | Ends the program with a status, which becomes `ERRORLEVEL` |
| 34, Argument | Gives the program's path, as `A:\NAME.EXE`, and its arguments |
| 35, Abort | Shows the message of a runtime error, such as a `null` pointer in Ore, and ends the program with status 1 |
| 36, Clock | Gives MiniDos's date and time, which `DATE` and `TIME` may have moved |
| Sleep, time, ticks and random numbers, 31 to 33, 40 and 41 | Go to the VM |

A fault ends the program with a message, such as `Divide overflow` or `Stack overflow`, and status 255; so does any other privileged instruction. `HALT` ends it with status 0. Memory is not protected, as under DOS: a program that writes outside its own memory can damage MiniDos.

`ore/minidos/programs` holds the system programs, which `make` builds with `vmc -g0` and MiniDos carries inside itself for `FORMAT /S` and `SYS`:

- `SORT [/R] [/+n] [file]` writes the lines of a file or of its input in order, ignoring case, comparing them from column *n* on with `/+n` and in reverse order with `/R`.
- `FIND [/V] [/C] [/N] [/I] "text" [file]` writes the lines that contain the text, with `/V` those that do not, with `/C` only how many, with `/N` each after its number in brackets, and with `/I` ignoring case. It ends with status 1 when no line matched.

A new program is any Ore program that reads and writes with `std/io`, best built with `-g0` as well:

```console
$ ./vmc -g0 hello.ore
$ ./vm -b work.img minidos.bin
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
| 11 | Attributes: 0x01 read-only, 0x02 hidden, 0x04 system, 0x10 directory, 0x20 archive |
| 12 | First cluster, 0 for an empty file |
| 16 | Size in bytes |
| 20 | When it was last written, in seconds since 1970-01-01 00:00 of the local time, or 0 for unknown |
| 24 | Reserved, 8 bytes |

## Source

| File | |
|---|---|
| `ore/minidos/main.ore` | Starting up: checking the drives, offering to format drive A and running `AUTOEXEC.BAT` |
| `ore/minidos/shell.ore` | The prompt, running commands and programs, redirection, pipes and batch files |
| `ore/minidos/commands.ore` | The table of commands with their help |
| `ore/minidos/args.ore` | Splitting a command's text into parameters and switches |
| `ore/minidos/files.ore` | The commands for files and directories |
| `ore/minidos/disks.ore` | `FORMAT`, `LABEL`, `VOL`, `CHKDSK` and `DISKCOPY` |
| `ore/minidos/system.ore` | The system programs, `SYS`, `DATE`, `TIME`, `MEM` and the host's files |
| `ore/minidos/editor.ore` | `EDIT` |
| `ore/minidos/console.ore` | Output and input, redirected or not, and the line editor |
| `ore/minidos/program.ore` | Loading programs, serving their syscalls and ending them after faults |
| `ore/minidos/fs.ore` | The file system |
| `ore/minidos/disk.ore` | The disk ports |
| `ore/minidos/time.ore` | The clock and how dates and times are written |
| `ore/minidos/names.ore` | Comparing and joining names and paths |
| `ore/minidos/programs/` | `SORT` and `FIND` |

`tests/minidos` holds MiniDos sessions for the test suite: what is typed in `NAME.in` and what MiniDos shows in `NAME.out`, with the display as plain text frames, along with programs the sessions import. They run with the clock held at 2026-10-01 9:30.

## Limits

- One program at a time, with 1 MB for its code, data and stack, and no protection of MiniDos's memory from it.
- No interrupts: a program that never ends can only be stopped with the VM.
- Names of 8 and 3 characters, and disks of at most 32 MB.
- `DATE` and `TIME` last until MiniDos stops.
