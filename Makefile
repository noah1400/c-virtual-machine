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

all: vm vmasm vmld vmc0

vm: $(VM_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ -lm

vmasm: $(ASM_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

vmld: $(LD_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

vmc0: $(VMC_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -c -o $@ $<

test: vm vmasm vmld vmc0
	sh tests/run.sh $(T)

clean:
	rm -rf $(BUILD) vm vmasm vmld vmc0

-include $(VM_OBJ:.o=.d) $(ASM_OBJ:.o=.d) $(LD_OBJ:.o=.d) $(VMC_OBJ:.o=.d)

.PHONY: all test clean
