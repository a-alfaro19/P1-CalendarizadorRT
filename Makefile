CC       = gcc
CFLAGS   = -std=gnu11 -Wall -Wextra -Wpedantic -g -Iinclude
SANFLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer

BUILD_DIR := build
BIN_DIR   := bin
LIB_DIR   := libs
TEST_DIR  := tests

HEADERS   := $(shell find include -name "*.h")

# ---------- Static libs ----------
LIB_PROTOCOL  := $(LIB_DIR)/libprotocol.a
LIB_SCHEDULER := $(LIB_DIR)/libscheduler.a
LIB_HOST_COMM := $(LIB_DIR)/libhost_comm.a

# Protocol objects
PROTOCOL_SRCS := $(shell find src/protocol -name "*.c")
PROTOCOL_OBJS := $(patsubst src/protocol/%.c,$(BUILD_DIR)/obj/protocol/%.o,$(PROTOCOL_SRCS))

# Scheduler objects
SCHEDULER_SRCS := $(shell find src/host/scheduler -name "*.c")
SCHEDULER_OBJS := $(patsubst src/host/scheduler/%.c,$(BUILD_DIR)/obj/host/scheduler/%.o,$(SCHEDULER_SRCS))

# Host Commons objects
HOST_COMM_SRCS := src/host/scheduler_runtime.c src/host/ipc_queue.c src/host/ring_buffer.c src/host/shared_memory.c src/host/node_driver.c src/host/process_manager.c
HOST_COMM_OBJS := $(patsubst src/host/%.c,$(BUILD_DIR)/obj/host/comm/%.o,$(HOST_COMM_SRCS))

# Host binary
HOST_SRC  := src/host/host.c
HOST_BIN  := $(BIN_DIR)/host_app

# Test files
TEST_SRCS := $(wildcard $(TEST_DIR)/*_tests.c)
TEST_BINS := $(patsubst $(TEST_DIR)/%.c,$(BUILD_DIR)/$(TEST_DIR)/%,$(TEST_SRCS))
NODE_SCHEDULER_SRCS := src/node/scheduler/calendarizador.c src/node/scheduler/evq.c
NODE_SCHEDULER_HEADERS := include/node/scheduler/calendarizador.h include/node/scheduler/evq.h include/node/scheduler/sched_config.h
NODE_DRIVER_CORE_SRCS := src/node/node_driver.c $(NODE_SCHEDULER_SRCS) $(PROTOCOL_SRCS)

.DEFAULT_GOAL := all
.PHONY: all host test-all clean

all: host

# ---------- Static Libs Rules ----------

$(LIB_PROTOCOL): $(PROTOCOL_OBJS)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(LIB_SCHEDULER): $(SCHEDULER_OBJS)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(LIB_HOST_COMM): $(HOST_COMM_OBJS)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

# ---------- Objects Compilation Rules ----------

$(BUILD_DIR)/obj/protocol/%.o: src/protocol/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/obj/host/scheduler/%.o: src/host/scheduler/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/obj/host/comm/%.o: src/host/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# ---------- Host Rules ----------

host: $(LIB_PROTOCOL) $(LIB_HOST_COMM) $(LIB_SCHEDULER) $(HOST_BIN)

$(HOST_BIN): $(HOST_SRC) $(LIB_PROTOCOL) $(LIB_HOST_COMM) $(LIB_SCHEDULER)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -L$(LIB_DIR) -lhost_comm -lscheduler -lprotocol -lm -pthread -o $@

# ---------- Tests Rules ----------

# Node scheduler tests
$(BUILD_DIR)/$(TEST_DIR)/node_scheduler_tests: $(TEST_DIR)/node_scheduler_tests.c $(NODE_SCHEDULER_SRCS) $(NODE_SCHEDULER_HEADERS) $(TEST_DIR)/test_harness.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude/node/scheduler -Itests $< $(NODE_SCHEDULER_SRCS) -o $@

# Portable node driver core tests
$(BUILD_DIR)/$(TEST_DIR)/node_driver_core_tests: $(TEST_DIR)/node_driver_core_tests.c $(NODE_DRIVER_CORE_SRCS) $(HEADERS) $(TEST_DIR)/test_harness.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude/node/scheduler -Itests $< $(NODE_DRIVER_CORE_SRCS) -o $@

# Instrument the IPC implementation itself, not only the test executable.
$(BUILD_DIR)/$(TEST_DIR)/ipc_queue_tests: $(TEST_DIR)/ipc_queue_tests.c src/host/ipc_queue.c src/host/shared_memory.c src/host/ring_buffer.c $(HEADERS) $(TEST_DIR)/test_harness.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SANFLAGS) -Itests $< src/host/ipc_queue.c src/host/shared_memory.c src/host/ring_buffer.c -pthread -o $@

# Instrumentar también la implementación del planificador y sus controladores.
$(BUILD_DIR)/$(TEST_DIR)/scheduler_runtime_tests $(BUILD_DIR)/$(TEST_DIR)/node_driver_tests: $(BUILD_DIR)/$(TEST_DIR)/%: $(TEST_DIR)/%.c $(HOST_COMM_SRCS) $(SCHEDULER_SRCS) $(PROTOCOL_SRCS) $(HEADERS) $(TEST_DIR)/test_harness.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SANFLAGS) -Itests $< $(HOST_COMM_SRCS) $(SCHEDULER_SRCS) $(PROTOCOL_SRCS) -lm -pthread -o $@

# Generic compilation for any test
$(BUILD_DIR)/$(TEST_DIR)/%: $(TEST_DIR)/%.c $(LIB_PROTOCOL) $(LIB_HOST_COMM) $(LIB_SCHEDULER) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SANFLAGS) -Itests $< -L$(LIB_DIR) -lhost_comm -lscheduler -lprotocol -lm -pthread -o $@
	
$(BUILD_DIR)/$(TEST_DIR)/%/:
	@mkdir -p $@

# Run all tests
test-all: $(TEST_BINS)
	@fail=0; \
	for t in $(TEST_BINS); do \
		echo "==> Ejecutando $$t"; \
		./$$t || fail=1; \
	done; \
	if [ $$fail -eq 0 ]; then echo "TODOS LOS TESTS PASARON EXITOSAMENTE"; \
	else echo "ALGUNOS TESTS FALLARON"; exit 1; fi

# Run a single test: make test-protocol
test-%: $(BUILD_DIR)/$(TEST_DIR)/%_tests
	./$<

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) $(LIB_DIR)
