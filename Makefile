TARGET = muse
CC = gcc
CFLAGS = -Wall -std=c99
SRCS = src/*.c

muse:
	$(CC) $(CFLAGS) -o $@ $(SRCS)

.PHONY: clean

clean:
	rm muse
