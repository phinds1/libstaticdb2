CC      = musl-gcc
CFLAGS  = -std=c11 -Wall -Wextra -I ../include -I src -static
LDFLAGS = ../libstaticdb2.a -lm

SNIPRUN = ../snip/sniprun

SRC = src/main.c src/args.c src/conn.c src/changelog.c src/delta.c src/scan.c src/sha256.c src/engine.c

CPPCHECK = cppcheck
CPPCHECK_FLAGS = --enable=warning,style,performance --std=c11 \
                 --suppress=missingIncludeSystem \
                 --suppress=checkersReport \
                 -I ../include -I src \
                 --error-exitcode=1 --quiet

.PHONY: all test lint clean

all: db2delta

db2delta: $(SRC)
	$(CC) $(CFLAGS) -o db2delta $(SRC) -x none $(LDFLAGS)

test:
	$(SNIPRUN) test/d1_args.in
	$(SNIPRUN) test/d2_conn.in
	$(SNIPRUN) test/d3_changelog.in
	$(SNIPRUN) test/d4_delta.in
	$(SNIPRUN) test/d5_scan.in
	$(SNIPRUN) test/d6_sha256.in
	$(SNIPRUN) test/d7_engine.in
	$(SNIPRUN) test/d8_validation.in
	$(SNIPRUN) test/d9_env.in
# d_integration skipped in unit test run — needs DB2_TEST_HOST
# Run via: local-integration/tests/run-db2delta-integration.sh

lint:
	$(CPPCHECK) $(CPPCHECK_FLAGS) $(SRC)

clean:
	rm -f db2delta
	rm -f test/d1_args    test/run_d1_args
	rm -f test/d2_conn    test/run_d2_conn
	rm -f test/d3_changelog test/run_d3_changelog
	rm -f test/d4_delta   test/run_d4_delta
	rm -f test/d5_scan    test/run_d5_scan
	rm -f test/d6_sha256  test/run_d6_sha256
	rm -f test/d7_engine  test/run_d7_engine
	rm -f test/d8_validation test/run_d8_validation
	rm -f test/d9_env     test/run_d9_env
	rm -f test/d_integration test/run_d_integration
