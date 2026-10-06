CC       = gcc
CFLAGS   = -std=gnu11 -Wall -Wextra -Wpedantic -g -Iinclude
# Sanitizers detect out-of-bounds accesses and undefined behaviour in tests
SANFLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer

BUILD_DIR := build
TEST_DIR  := tests
HEADERS   := $(wildcard include/*.h)

TEST_SRCS := $(wildcard $(TEST_DIR)/*_tests.c)
TEST_BINS := $(patsubst $(TEST_DIR)/%.c,$(BUILD_DIR)/$(TEST_DIR)/%,$(TEST_SRCS))

.DEFAULT_GOAL := test-all
.PHONY: test-all clean

# ---------- Sources needed by each test program ----------
# One line per tests/<name>.c file. If the protocol is tested split by module
# later, add one line per new test file with only the sources it needs.
PROTOCOL_SRCS := src/crc16.c src/frame.c src/parser.c src/message.c

$(BUILD_DIR)/$(TEST_DIR)/protocol_tests: $(PROTOCOL_SRCS)

# ---------- Rules ----------
# Build any test program: tests/<name>.c + the sources listed above
$(BUILD_DIR)/$(TEST_DIR)/%: $(TEST_DIR)/%.c $(HEADERS)
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

# Run a single test program: make test-protocol  (runs tests/protocol_tests.c)
test-%: $(BUILD_DIR)/$(TEST_DIR)/%_tests
	./$<

clean:
	rm -rf $(BUILD_DIR)
