CC       ?= gcc
CFLAGS   ?= -O2 -g
BUILD    := build

ALL_CFLAGS   := -std=c11 -Wall -Wextra $(CFLAGS)
ALL_CPPFLAGS := -Iinclude -D_POSIX_C_SOURCE=200809L -MMD -MP $(CPPFLAGS)

VM_SRC  := $(wildcard src/*.c src/core/*.c src/io/*.c src/common/*.c)
VM_OBJ  := $(VM_SRC:%.c=$(BUILD)/%.o)
ASM_SRC := $(wildcard assembler/*.c src/common/*.c)
ASM_OBJ := $(ASM_SRC:%.c=$(BUILD)/%.o)
LD_SRC  := $(wildcard linker/*.c src/common/*.c)
LD_OBJ  := $(LD_SRC:%.c=$(BUILD)/%.o)
VMC_SRC := $(wildcard ore/bootstrap/*.c)
VMC_OBJ := $(VMC_SRC:%.c=$(BUILD)/%.o)
ORE_SRC := $(wildcard ore/compiler/*.ore ore/lib/*.asm ore/lib/std/*)
ORE_LIB := $(wildcard ore/lib/*.asm ore/lib/std/*)
MINIDOS_SRC := $(wildcard ore/minidos/*.ore ore/minidos/*.asm)
MINIDOS_PROGRAMS := $(patsubst ore/minidos/programs/%.ore,$(BUILD)/minidos/%.bin,$(wildcard ore/minidos/programs/*.ore))

all: vm vmasm vmld vmc0 vmc.bin minidos.bin minidos.img

vm: $(VM_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ -lm

vmasm: $(ASM_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

vmld: $(LD_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

vmc0: $(VMC_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

# The Ore compiler written in Ore: vmc0 compiles it into vmc1.bin, which compiles it again into vmc.bin,
# the compiler that the vmc script runs on the VM
vmc1.bin: vmc0 vmasm $(ORE_SRC)
	./vmc0 ore/compiler/vmc.ore -o $@

vmc.bin: vmc1.bin vm vmasm
	./vm -m 262144 -S 8192 ./vmc1.bin ore/compiler/vmc.ore -o $@
	./vmasm -I ./ore/lib -I . $@.asm -o $@
	rm -f $@.asm

# MiniDos lives above the programs it runs, which start at 0, asks for 4 MB of memory and carries the
# programs that FORMAT /S puts onto a disk
minidos.bin: vmc.bin vm vmasm $(MINIDOS_SRC) $(ORE_LIB) $(MINIDOS_PROGRAMS)
	./vmc -b 0x100000 -m 4096 -I $(BUILD)/minidos ore/minidos/main.ore -o $@

# MiniDos only loads the code and data of a program, so its programs leave out debug information
$(BUILD)/minidos/%.bin: ore/minidos/programs/%.ore vmc.bin vm vmasm $(ORE_LIB)
	@mkdir -p $(dir $@)
	./vmc -g0 $< -o $@

# A 4 MB disk with the system programs on it, which MiniDos formats when it finds the disk new, answering
# yes, 4M and the label MINIDOS
minidos.img: minidos.bin
	rm -f $@
	printf 'Y\n4M\nMINIDOS\nEXIT\n' | ./vm -b $@ minidos.bin > /dev/null

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -c -o $@ $<

test: vm vmasm vmld vmc0 vmc.bin minidos.bin
	sh tests/run.sh $(T)

clean:
	rm -rf $(BUILD) vm vmasm vmld vmc0 vmc1.bin vmc.bin minidos.bin minidos.img

-include $(VM_OBJ:.o=.d) $(ASM_OBJ:.o=.d) $(LD_OBJ:.o=.d) $(VMC_OBJ:.o=.d)

.PHONY: all test clean
