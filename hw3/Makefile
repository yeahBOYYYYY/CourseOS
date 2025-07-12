# Kernel module settings
obj-m += message_slot.o

# Use uname to find correct kernel source path
KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

# Compiler flags for user programs
CC = gcc
CFLAGS = -O3 -Wall -std=c11

.PHONY: all clean

# Build everything
all: message_slot.ko message_sender message_reader

# Build kernel module
message_slot.ko:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# Build sender and reader
message_sender: message_sender.c message_slot.h
	$(CC) $(CFLAGS) -o message_sender message_sender.c

message_reader: message_reader.c message_slot.h
	$(CC) $(CFLAGS) -o message_reader message_reader.c

# Clean generated files
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f message_sender message_reader
