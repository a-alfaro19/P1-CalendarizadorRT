CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -Iinclude
LDLIBS = -lm

TARGET = host

SRC = src/main.c \
      src/config.c \
      src/task_parser.c \
      src/admission.c \
      src/planner.c

OBJ = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET) config/config.txt 20 12

test-failure: $(TARGET)
	./$(TARGET) config/config_failure.txt 10 10 --fail 1

test-edf: $(TARGET)
	./$(TARGET) config/config_edf.txt 10 10

test-rms: $(TARGET)
	./$(TARGET) config/config_rms.txt 10 10

test-ff: $(TARGET)
	./$(TARGET) config/config_ff.txt 10 10

test-wf: $(TARGET)
	./$(TARGET) config/config_wf.txt 10 10

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all run test-failure test-edf test-rms test-ff test-wf clean
