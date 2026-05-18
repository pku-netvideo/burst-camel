# Makefile for CAMEL-CC

CC = gcc
CFLAGS = -Wall -Wextra -I./src/include -g -O2
LDFLAGS =

# Directories
SRC_DIR = src
TEST_DIR = test
BUILD_DIR = build

# Source files
SRC_FILES = $(wildcard $(SRC_DIR)/*.c)
OBJ_FILES = $(SRC_FILES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Test executable
TEST_EXEC = $(TEST_DIR)/fcc_unittest
TEST_SOURCES = $(wildcard $(TEST_DIR)/*_test.c) $(TEST_DIR)/network_simulator.c $(TEST_DIR)/main.c $(TEST_DIR)/test_framework.c
TEST_OBJECTS = $(TEST_SOURCES:%.c=$(BUILD_DIR)/%_test.o)

.PHONY: all clean test

all: $(BUILD_DIR) $(OBJ_FILES) $(TEST_EXEC)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_EXEC): $(TEST_OBJECTS) $(OBJ_FILES)
	$(CC) $(CFLAGS) $^ -o $@ -lm

$(BUILD_DIR)/%_test.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_EXEC)
	./$(TEST_EXEC)

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(TEST_EXEC)

# Coverage report (requires gcov)
coverage: CFLAGS += --coverage
coverage: clean all
	./$(TEST_EXEC)
	gcov -o $(BUILD_DIR) $(SRC_DIR)/*.c

# Static analysis (requires cppcheck)
analyze:
	cppcheck --enable=all --std=c11 $(SRC_DIR)/*.c

# Format code (requires clang-format)
format:
	clang-format -i $(SRC_DIR)/*.c $(SRC_DIR)/*.h $(TEST_DIR)/*.c
