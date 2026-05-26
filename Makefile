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
	@./tools/runtests.sh

test-one: $(OUT) $(BFRUN)
	@./tools/runtests.sh -f $(TEST)

test-strict: $(OUT) $(BFRUN)
	@./tools/runtests.sh --strict

# Проверка самого runtests.sh
test-system: $(OUT) $(BFRUN)
	@echo "=== Testing runtests.sh itself ==="
	@for f in tests/test_runtests/*.bfasm; do \
		./tools/runtests.sh -f "$$f"; \
	done
