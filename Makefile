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
	@./$(OUT) tests/test_hello.bfasm > /tmp/_bf.bf && $(BFRUN) /tmp/_bf.bf
	@echo
	@echo "=== Test 2: Increment input (A -> B) ==="
	@./$(OUT) tests/test_inc_input.bfasm > /tmp/_bf.bf && echo -n 'A' | $(BFRUN) /tmp/_bf.bf
	@echo
	@echo "=== Test 3: Double input (byte 0x82) ==="
	@./$(OUT) tests/test_double.bfasm > /tmp/_bf.bf && echo -n 'A' | $(BFRUN) /tmp/_bf.bf | xxd
	@echo "=== Test 4: Macro ADD from include (A+B=0x83) ==="
	@./$(OUT) tests/test_include.bfasm > /tmp/_bf.bf && echo -n 'AB' | $(BFRUN) /tmp/_bf.bf | xxd
	@echo "=== Test 5: AS and @ (3*2=6) ==="
	@./$(OUT) tests/test_as_kep.bfasm > /tmp/_bf.bf && $(BFRUN) /tmp/_bf.bf | xxd
	@echo "=== Test 6: SUB and MOV_SAFE (10-3=7) ==="
	@./$(OUT) tests/test_sub_safe.bfasm > /tmp/_bf.bf && $(BFRUN) /tmp/_bf.bf | xxd
	@rm -f /tmp/_bf.bf
	@echo "=== Test raw BF ==="
	@./$(OUT) tests/test_rawbf.bfasm > /tmp/_raw.bf && $(BFRUN) /tmp/_raw.bf | xxd
	@echo "=== Test begin BF ==="
	@./$(OUT) tests/test_beginbf.bfasm > /tmp/_raw.bf && $(BFRUN) /tmp/_raw.bf | xxd
		@echo "=== Test 7: MUL (4*3=12) ==="
	@./$(OUT) tests/lib/test_mul.bfasm > /tmp/_bf.bf && $(BFRUN) /tmp/_bf.bf | xxd
		@echo "=== Test NEG (0 -> 1) ==="
	@./$(OUT) tests/lib/test_neg.bfasm > /tmp/_bf.bf && $(BFRUN) /tmp/_bf.bf | xxd
	@echo "=== Test CMP equal (5==5 -> 1) ==="
	@./$(OUT) tests/lib/test_cmp.bfasm > /tmp/_bf.bf && $(BFRUN) /tmp/_bf.bf | xxd
		@echo "=== Test SWAP and ISEQ ==="
	@./$(OUT) tests/lib/test_swap_iseq.bfasm > /tmp/_bf.bf && $(BFRUN) /tmp/_bf.bf | xxd
	@rm -f /tmp/_bf.bf
