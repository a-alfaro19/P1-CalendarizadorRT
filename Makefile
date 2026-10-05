CC       = gcc
CFLAGS   = -std=gnu11 -Wall -Wextra -Wpedantic -g -Iinclude
# Sanitizers detect out-of-bounds accesses and undefined behaviour in tests
SANFLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer

BUILD_DIR := build
TEST_DIR  := tests
HEADERS   := $(wildcard include/*.h)

# Every tests/*_tests.c file is a test program (it has its own main)
TEST_SRCS := $(wildcard $(TEST_DIR)/*_tests.c)
TEST_BINS := $(patsubst $(TEST_DIR)/%.c,$(BUILD_DIR)/$(TEST_DIR)/%,$(TEST_SRCS))

.DEFAULT_GOAL := test-all
.PHONY: test-all clean

# Build one test binary: tests/X_tests.c + src/X.c
# If a test needs another module, add its source as an extra prerequisite:
#   $(BUILD_DIR)/$(TEST_DIR)/parser_tests: src/protocol.c
$(BUILD_DIR)/$(TEST_DIR)/%_tests: $(TEST_DIR)/%_tests.c src/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SANFLAGS) $(filter %.c,$^) -o $@

# Run every test and report a summary (does not stop at the first failure)
test-all: $(TEST_BINS)
	@fail=0; \
	for t in $(TEST_BINS); do \
		echo "==> $$t"; \
		./$$t || fail=1; \
	done; \
	if [ $$fail -eq 0 ]; then echo "ALL TEST PROGRAMS PASSED"; \
	else echo "SOME TEST PROGRAMS FAILED"; exit 1; fi

# Run a single module: make test-protocol
test-%: $(BUILD_DIR)/$(TEST_DIR)/%_tests
	./$<

clean:
	rm -rf $(BUILD_DIR)
