CFLAGS = -Wall -Wextra -std=c99 -pedantic
SRC = src/main.c src/parser.c src/codegen.c
OUT = bfasm
BFRUN = tools/bfrun

.PHONY: all clean test

all: $(OUT) $(BFRUN)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

$(BFRUN): tools/bfrun.c
	$(CC) -O2 -o $@ $^

clean:
	rm -f $(OUT) $(BFRUN)

test: $(OUT) $(BFRUN)
	@echo "=== Test 1: Hello (H) ==="
	@./$(OUT) tests/test_hello.bfasm > /tmp/_bfasm_test.bf && $(BFRUN) /tmp/_bfasm_test.bf
	@echo
	@echo "=== Test 2: Increment input (A -> B) ==="
	@./$(OUT) tests/test_inc_input.bfasm > /tmp/_bfasm_test.bf && echo -n 'A' | $(BFRUN) /tmp/_bfasm_test.bf
	@echo
	@echo "=== Test 3: Double input (should print byte 0x82) ==="
	@./$(OUT) tests/test_double.bfasm > /tmp/_bfasm_test.bf && echo -n 'A' | $(BFRUN) /tmp/_bfasm_test.bf | xxd
	@rm -f /tmp/_bfasm_test.bf
