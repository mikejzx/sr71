OUT=gem
SRC=*.c
CFLAGS=-Og -g -D_GNU_SOURCE
LDFLAGS=-lcrypto -ltls -lssl
CC=gcc

all: $(OUT)

$(OUT): $(SRC) *.h
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRC) -o $(OUT)

run: all
	./$(OUT)

debug: all
	gdb ./$(OUT)

clean:
	rm $(OUT)
