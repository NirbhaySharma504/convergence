# Convergence — build, test, demo, bench.
CC      := gcc
CFLAGS  := -std=gnu11 -Wall -Wextra -O2 -Iinclude
LDFLAGS := -pthread

# CRDT + core object files shared by the server and tests.
CRDT_SRC := src/crdt/vclock.c src/crdt/gcounter.c src/crdt/pncounter.c \
            src/crdt/lww_register.c src/crdt/orset.c
CORE_SRC := src/net.c src/node.c src/wal.c
COMMON   := $(CRDT_SRC) $(CORE_SRC)

.PHONY: all test demo bench clean

all: convergence convergence-cli

demo: all
	@bash demo/partition_demo.sh

bench: all | build
	$(CC) $(CFLAGS) bench/bench.c src/crdt/gcounter.c src/wal.c src/net.c -o build/bench $(LDFLAGS)
	@./build/bench

convergence: $(COMMON) src/main.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

convergence-cli: src/net.c src/cli.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# ---- tests ----------------------------------------------------------------
TESTS := test_vclock test_gcounter test_pncounter test_lww test_orset test_wal test_integration

test_vclock: tests/test_vclock.c src/crdt/vclock.c | build
	$(CC) $(CFLAGS) $^ -o build/$@ $(LDFLAGS)

test_gcounter: tests/test_gcounter.c src/crdt/gcounter.c | build
	$(CC) $(CFLAGS) $^ -o build/$@ $(LDFLAGS)

test_pncounter: tests/test_pncounter.c src/crdt/pncounter.c src/crdt/gcounter.c | build
	$(CC) $(CFLAGS) $^ -o build/$@ $(LDFLAGS)

test_lww: tests/test_lww.c src/crdt/lww_register.c src/crdt/vclock.c | build
	$(CC) $(CFLAGS) $^ -o build/$@ $(LDFLAGS)

test_orset: tests/test_orset.c src/crdt/orset.c | build
	$(CC) $(CFLAGS) $^ -o build/$@ $(LDFLAGS)

test_wal: tests/test_wal.c src/node.c src/wal.c $(CRDT_SRC) | build
	$(CC) $(CFLAGS) $^ -o build/$@ $(LDFLAGS)

test_integration: tests/test_integration.c src/net.c convergence | build
	$(CC) $(CFLAGS) tests/test_integration.c src/net.c -o build/$@ $(LDFLAGS)

test: all | build
	@$(MAKE) -s $(TESTS)
	@echo "=== running unit + integration tests ==="
	@for t in $(TESTS); do ./build/$$t || exit 1; done
	@echo "=== all tests passed ==="

build:
	@mkdir -p build

clean:
	rm -rf build convergence convergence-cli *.wal node*.wal
