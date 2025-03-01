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