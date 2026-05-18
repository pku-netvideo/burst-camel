# Makefile for CAMEL-CC tests

CC = gcc
CFLAGS = -Wall -Wextra -I../src/include -g -O2
LDFLAGS = -lm

TEST_EXEC = fcc_unittest

.PHONY: all clean test

all: $(TEST_EXEC)

$(TEST_EXEC): main.c test_framework.c test_framework.h
	$(CC) $(CFLAGS) main.c test_framework.c -o $(TEST_EXEC) $(LDFLAGS)

test: $(TEST_EXEC)
	./$(TEST_EXEC)

clean:
	rm -f $(TEST_EXEC) *.o

run:
	./$(TEST_EXEC)
