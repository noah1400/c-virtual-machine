CC       ?= gcc
CFLAGS   ?= -O2 -g
BUILD    := build

ALL_CFLAGS   := -std=c11 -Wall -Wextra $(CFLAGS)
ALL_CPPFLAGS := -Iinclude -D_POSIX_C_SOURCE=200809L -MMD -MP $(CPPFLAGS)

VM_SRC  := $(wildcard src/*.c src/core/*.c src/io/*.c src/common/*.c)
VM_OBJ  := $(VM_SRC:%.c=$(BUILD)/%.o)
ASM_SRC := $(wildcard assembler/*.c src/common/*.c)
ASM_OBJ := $(ASM_SRC:%.c=$(BUILD)/%.o)

all: vm vmasm

vm: $(VM_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

vmasm: $(ASM_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD) vm vmasm

-include $(VM_OBJ:.o=.d) $(ASM_OBJ:.o=.d)

.PHONY: all clean
