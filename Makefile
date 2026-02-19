CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g
SRC = $(wildcard src/*.c) $(wildcard src/views/*.c)
OBJ = $(SRC:.c=.o)
TARGET = tuide

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
