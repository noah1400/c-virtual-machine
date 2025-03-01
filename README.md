After reviewing my previous projects [c64vm](https://github.com/noah1400/c64vm) and [c16vm](https://github.com/noah1400/c16vm)
I decided to rewrite the vm. It includes a disassembler and a debugger. Some features like interrupt handling different syscalls and devices are still a WIP

# VM

WIP

# build

```bash	
make
```

## Usage
```bash
./vm <input_file>
```

disasm

```bash
./vm -D <input_file>
```

debugger

```bash
./vm -d <input_file>
```

help

```bash
./vm -h
```

## test

```
// generate file
python3 tools/generate_test_program.py > bin/fib
// run
./vm bin/fib
```