# File: Makefile
CC = gcc
CFLAGS = -std=c11 -pedantic -Wall -Wextra -g
OBJ = main.o parser.o macros.o

.PHONY: all clean

all: proj1

proj1: $(OBJ)
	$(CC) $(CFLAGS) -o proj1 $(OBJ)

main.o: main.c parser.h macros.h
parser.o: parser.c parser.h macros.h
macros.o: macros.c macros.h

clean:
	rm -f proj1 *.o
