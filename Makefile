OUT=gem

SRCS=$(shell ls *.c)
OBJS=$(patsubst %.c,%.o,$(SRCS))
DEPS=$(patsubst %.c,%.d,$(SRCS))

CFLAGS=-Og -g -D_GNU_SOURCE -Wpedantic -Wall
LDFLAGS=-lcrypto -lssl
CC=gcc
RM=rm -f

.PHONY: all clean run debug

all: $(OUT)

run: all
	./$(OUT)

# Runs with GDB debugger
debug: all
	gdb ./$(OUT)

# Run with Valgrind
mem: all
	valgrind --leak-check=full --show-leak-kinds=all \
		--track-origins=yes \
		--log-file=valgrind-out.txt \
		./$(OUT)

clean:
	$(RM) $(OBJS)
	$(RM) $(OUT)

# Link objs to executable
$(OUT): $(OBJS)
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS)

# Include generated dependencies
-include $(DEPS)

# Compile objects with dependencies
%.o: %.c Makefile
	$(CC) -MMD -MP -c $< -o $@ $(CFLAGS) $(LDFLAGS)
