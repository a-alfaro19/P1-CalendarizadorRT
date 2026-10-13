CC       = gcc
CFLAGS   = -std=gnu11 -Wall -Wextra -Wpedantic -g -Iinclude
SANFLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer

BUILD_DIR := build
BIN_DIR   := bin
LIB_DIR   := libs
TEST_DIR  := tests

HEADERS   := $(shell find include -name "*.h")

# Protocol source and object files
PROTOCOL_SRCS := $(shell find src/protocol -name "*.c")
PROTOCOL_OBJS := $(patsubst src/protocol/%.c,$(BUILD_DIR)/obj/protocol/%.o,$(PROTOCOL_SRCS))

# Protocol static lib
LIB_PROTOCOL  := $(LIB_DIR)/libprotocol.a

# Host source files
HOST_SRC  := src/host/host.c
HOST_BIN  := $(BIN_DIR)/host_app

# Test files
TEST_SRCS := $(wildcard $(TEST_DIR)/*_tests.c)
TEST_BINS := $(patsubst $(TEST_DIR)/%.c,$(BUILD_DIR)/$(TEST_DIR)/%,$(TEST_SRCS))

.DEFAULT_GOAL := test-all
.PHONY: test-all host clean

# ---------- Protocol Lib Generation Rules ----------
$(LIB_PROTOCOL): $(PROTOCOL_OBJS)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

# Regla para compilar los objetos del protocolo
$(BUILD_DIR)/obj/protocol/%.o: src/protocol/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# ---------- Host Rules ----------
$(HOST_BIN): $(HOST_SRC) $(LIB_PROTOCOL) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -L$(LIB_DIR) -lprotocol -o $@

host: $(HOST_BIN)

# ---------- Tests Rules ----------
$(BUILD_DIR)/$(TEST_DIR)/%: $(TEST_DIR)/%.c src/host/node_driver.c src/host/ring_buffer.c $(LIB_PROTOCOL) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SANFLAGS) -Itests $< src/host/node_driver.c src/host/ring_buffer.c -L$(LIB_DIR) -lprotocol -o $@

# Run every test and report a summary
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
	rm -rf $(BUILD_DIR) $(BIN_DIR) $(LIB_DIR)
