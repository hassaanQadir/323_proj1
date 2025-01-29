# File: Makefile

CC = gcc
CFLAGS = -std=c11 -pedantic -Wall -Wextra -g

.PHONY: all clean

all: proj1

proj1: proj1.o
	$(CC) $(CFLAGS) -o proj1 proj1.o

proj1.o: proj1.c proj1.h
	$(CC) $(CFLAGS) -c proj1.c -o proj1.o

clean:
	rm -f proj1 *.o
