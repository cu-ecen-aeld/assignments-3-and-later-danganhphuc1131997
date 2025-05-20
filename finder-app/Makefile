COMPILER ?= gcc

ifeq ($(strip $(COMPILER)), aarch64-none-linux-gnu-)
	override COMPILER = aarch64-none-linux-gnu-gcc
endif

default:
	@echo "Compiler: '$(COMPILER)'"
	$(COMPILER) writer.c -o writer

clean:
	rm -rf *.o writer

