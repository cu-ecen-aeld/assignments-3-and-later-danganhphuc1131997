CC = gcc
TARGET = aesdsocket
SRCS = $(wildcard *.c)

ifdef CROSS_COMPILE
	CC = $(CROSS_COMPILE)gcc
endif

all:
	$(CC) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)