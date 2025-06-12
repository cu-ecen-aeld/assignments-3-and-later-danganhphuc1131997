# Compiler and flags
CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -Wall -Wextra -O2
LDFLAGS ?= -pthread
INCLUDE = -I.

# Target executable and source files
TARGET := aesdsocket
SRCS := $(TARGET).c
OBJS := $(SRCS:.c=.o)

# Phony targets
.PHONY: all clean

# Default target
all: $(TARGET)

# Link object files to create the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDE) $(LDFLAGS) -o $@ $^

# Compile source files to object files
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDE) -c -o $@ $<

# Clean build artifacts
clean:
	rm -f $(OBJS) $(TARGET)