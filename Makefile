CC = gcc
CFLAGS = -Wall -Wextra -pedantic -g
TARGET = proj1

all: $(TARGET)

$(TARGET): proj1.o
	$(CC) $(CFLAGS) -o $(TARGET) proj1.o

proj1.o: proj1.c proj1.h
	$(CC) $(CFLAGS) -c proj1.c

clean:
	rm -f $(TARGET) *.o

.PHONY: all clean
