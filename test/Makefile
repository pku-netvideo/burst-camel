CC = gcc
CFLAGS = -Wall -Wextra -I../src/include -I. -g -O2
LDFLAGS = -lm

TEST_SOURCES = main.c \
	test_framework.c \
	bandwidth_estimator_test.c \
	congestion_detector_test.c \
	burst_controller_test.c \
	feedback_test.c \
	receiver_aggregation_test.c \
	sender_synthetic_test.c \
	network_simulator.c \
	replay_test.c

TEST_OBJECTS = bandwidth_estimator.o congestion_detector.o burst_controller.o common.o receiver.o sender.o

TEST_EXEC = fcc_unittest

.PHONY: all clean test

all: $(TEST_OBJECTS) $(TEST_EXEC)

bandwidth_estimator.o: ../src/bandwidth_estimator.c
	$(CC) $(CFLAGS) -c ../src/bandwidth_estimator.c -o $@

congestion_detector.o: ../src/congestion_detector.c
	$(CC) $(CFLAGS) -c ../src/congestion_detector.c -o $@

burst_controller.o: ../src/burst_controller.c
	$(CC) $(CFLAGS) -c ../src/burst_controller.c -o $@

common.o: ../src/common.c
	$(CC) $(CFLAGS) -c ../src/common.c -o $@

receiver.o: ../src/receiver.c
	$(CC) $(CFLAGS) -c ../src/receiver.c -o $@

sender.o: ../src/sender.c
	$(CC) $(CFLAGS) -c ../src/sender.c -o $@

$(TEST_EXEC): $(TEST_SOURCES) $(TEST_OBJECTS)
	$(CC) $(CFLAGS) $(TEST_SOURCES) $(TEST_OBJECTS) -o $(TEST_EXEC) $(LDFLAGS)

test: $(TEST_EXEC)
	./$(TEST_EXEC)

clean:
	rm -f $(TEST_EXEC) *.o

run:
	./$(TEST_EXEC)
