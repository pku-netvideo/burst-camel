CC = gcc
CFLAGS = -Wall -Wextra -I../src/include -I. -g -O2
LDFLAGS = -lm

TEST_SOURCES = main.c \
	test_framework.c \
	bandwidth_estimator_test.c

TEST_OBJECTS = bandwidth_estimator.o

TEST_EXEC = fcc_unittest

.PHONY: all clean test

all: $(TEST_OBJECTS) $(TEST_EXEC)

$(TEST_OBJECTS): ../src/bandwidth_estimator.c
	$(CC) $(CFLAGS) -c ../src/bandwidth_estimator.c -o $@

$(TEST_EXEC): $(TEST_SOURCES) $(TEST_OBJECTS)
	$(CC) $(CFLAGS) $(TEST_SOURCES) $(TEST_OBJECTS) -o $(TEST_EXEC) $(LDFLAGS)

test: $(TEST_EXEC)
	./$(TEST_EXEC)

clean:
	rm -f $(TEST_EXEC) *.o

run:
	./$(TEST_EXEC)
