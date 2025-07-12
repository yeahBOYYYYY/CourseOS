CC := gcc
CFLAGS := -O3 -D_POSIX_C_SOURCE=200809 -Wall -std=c11

TARGETS := pcc_client pcc_server test1

all: $(TARGETS)

pcc_client: pcc_client.c
	$(CC) $(CFLAGS) $< -o $@

pcc_server: pcc_server.c
	$(CC) $(CFLAGS) $< -o $@

test1: test1.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGETS)

.PHONY: all clean
