CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -Iinclude
LDLIBS = -lm
TARGET = host
SRC = src/main.c src/config.c src/task_parser.c src/admission.c src/planner.c
OBJ = $(SRC:.c=.o)
all: $(TARGET)
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) $(LDLIBS)
src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@
run: $(TARGET)
	./$(TARGET) config/config.txt 20 12
clean:
	rm -f $(OBJ) $(TARGET)
.PHONY: all run clean
