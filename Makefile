# Makefile for bfasm
CFLAGS = -Wall -Wextra -std=c99 -pedantic
SRC = src/main.c src/parser.c src/codegen.c
OUT = bfasm

.PHONY: all clean test

all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(OUT)

test: $(OUT)
	@echo "=== Running tests ==="
	./$(OUT) tests/test_hello.bfasm | bf
	@echo
