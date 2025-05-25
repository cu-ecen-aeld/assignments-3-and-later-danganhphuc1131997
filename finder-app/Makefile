#all: CROSS_COMPILE
#deafult:
#	@gcc writer.c -o writer
#	@file writer
#CROSS_COMPILE:
#	@aarch64-none-linux-gnu-gcc writer.c -o writer
#	@file writer > ../assignments/assignment2/fileresult.txt
#clean:
#	@rm -f writer

CC = gcc

# Check if CROSS_COMPILE is existed
ifdef CROSS_COMPILE
	CC = $(CROSS_COMPILE)gcc
endif

all:
	@echo "Compiler: '$(CC)'"
	$(CC) -o writer writer.c

clean:
	rm -f writer
