MAKEFLAGS += --no-print-directory

.PHONY: all clean test run

all:
	$(MAKE) -C .. all

test:
	$(MAKE) -C .. test

clean:
	$(MAKE) -C .. clean
	rm -f *.o fcc_unittest

run: test
