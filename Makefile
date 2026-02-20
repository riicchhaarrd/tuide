CC = gcc
VERSION := $(shell git describe --tags --always 2>/dev/null || echo "unknown")
CFLAGS = -Wall -Wextra -std=c99 -g -DVERSION=\"$(VERSION)\"
SRC = $(wildcard src/*.c) $(wildcard src/views/*.c)
OBJ = $(SRC:.c=.o)
TARGET = tuide

UNAME_S := $(shell uname -s)

ifeq ($(OS),Windows_NT)
    LDFLAGS += -static
else ifeq ($(UNAME_S),Linux)
    LDFLAGS += -static
endif

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
