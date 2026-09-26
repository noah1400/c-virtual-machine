CC = gcc
CFLAGS = -Iinclude -g
LDFLAGS =

# Source directories
SRC_DIRS = src src/core src/io src/util

# Source files
SRC_FILES = $(wildcard src/*.c) \
            $(wildcard src/core/*.c) \
            $(wildcard src/io/*.c) \
            $(wildcard src/util/*.c)

# Object files
OBJ_FILES = $(SRC_FILES:.c=.o)

# Executable name
TARGET = vm

# Default target
all: directories $(TARGET)

# Create directories if they don't exist
directories:
	@mkdir -p src/core
	@mkdir -p src/io
	@mkdir -p src/util
	@mkdir -p include
	@mkdir -p bin

# Main executable
$(TARGET): $(OBJ_FILES)
	$(CC) $(LDFLAGS) -o $@ $^

# Compile source files
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Clean build artifacts
clean:
	rm -f $(OBJ_FILES) $(TARGET)

.PHONY: all clean install run debug disasm test_program newfile help directories