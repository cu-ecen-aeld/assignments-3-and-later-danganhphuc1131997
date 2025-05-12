COMPILER ?= gcc

ifeq ($(COMPILER), aarch64-none-linux-gnu-)
	COMPILER = aarch64-none-linux-gnu-gcc
else
	COMPILER = gcc
endif

default:
	$(COMPILER) writer.c -o writer

clean:
	rm -rf *.o writer