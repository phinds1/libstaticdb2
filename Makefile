CC      = musl-gcc
CFLAGS  = -std=c11 -Wall -Wextra -I include -I src -static
AR      = ar
ARFLAGS = rcs
SNIPRUN = ./snip/sniprun

LIB_SRC = src/buf.c src/net.c src/ccsid.c src/dss.c src/ddm.c src/drda.c src/sqlca.c src/fdoca.c src/handles.c src/api.c src/async.c
LIB_OBJ = $(LIB_SRC:.c=.o)

.PHONY: all lib test lint clean check db2delta db2delta-test db2delta-lint install

all: lib

lib: libstaticdb2.a

libstaticdb2.a: $(LIB_OBJ)
	$(AR) $(ARFLAGS) $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Run all phase snip tests
test: lib
	$(SNIPRUN) test/p1_buf.c.snip
	$(SNIPRUN) test/p2_ccsid.c.snip
	$(SNIPRUN) test/p3_dss.c.snip
	$(SNIPRUN) test/p4_ddm.c.snip
	$(SNIPRUN) test/p5_drda.c.snip
	$(SNIPRUN) test/p6_sqlca.c.snip
	$(SNIPRUN) test/p7_fdoca.c.snip
	$(SNIPRUN) test/p8_excsqlimm.c.snip
	$(SNIPRUN) test/p9_query.c.snip
	$(SNIPRUN) test/p10_decode.c.snip
	$(SNIPRUN) test/p11_odbc.c.snip
	$(SNIPRUN) test/p12_params.c.snip
	$(SNIPRUN) test/p13_txn.c.snip
	$(SNIPRUN) test/p14_async.c.snip
	$(SNIPRUN) test/p15_hard.c.snip
# p16 (integration) skipped in unit test run — needs DB2_TEST_HOST
# Run via: local-integration/tests/run-integration.sh

# Run unit tests + integration tests (requires a live DB2 instance).
# Set DB2_TEST_HOST or configure local-integration/local.env beforehand.
check: test integration

# Static analysis
lint:
	cppcheck --enable=style,performance,portability,warning \
	         --std=c11 --error-exitcode=1 \
	         --suppress=unusedFunction \
	         --suppress=missingIncludeSystem \
	         -I include -I src src/

# Integration tests against a live DB2 instance.
# Requires local-integration/bin/db2-run.sh + db2-create-testdb.sh first.
integration: lib
	local-integration/tests/run-integration.sh

clean:
	rm -f $(LIB_OBJ) libstaticdb2.a
	rm -f test/*.c
	rm -f test/p1_buf test/run_p1_buf
	rm -f test/p2_ccsid test/run_p2_ccsid
	rm -f test/p3_dss test/run_p3_dss
	rm -f test/p4_ddm test/run_p4_ddm
	rm -f test/p5_drda test/run_p5_drda
	rm -f test/p6_sqlca test/run_p6_sqlca
	rm -f test/p7_fdoca test/run_p7_fdoca
	rm -f test/p8_excsqlimm test/run_p8_excsqlimm
	rm -f test/p9_query test/run_p9_query
	rm -f test/p10_decode test/run_p10_decode
	rm -f test/p11_odbc test/run_p11_odbc
	rm -f test/p12_params test/run_p12_params
	rm -f test/p13_txn test/run_p13_txn
	rm -f test/p14_async test/run_p14_async
	rm -f test/p15_hard test/run_p15_hard
	rm -f test/p16_integration test/run_p16_integration

# ── db2delta sub-project ──────────────────────────────────────────────────────
db2delta: lib
	$(MAKE) -C db2delta

db2delta-test: db2delta
	$(MAKE) -C db2delta test

db2delta-lint: db2delta
	$(MAKE) -C db2delta lint

# ── Install ───────────────────────────────────────────────────────────────────
# Installs libstaticdb2 (headers + .a) system-wide, then installs db2delta
# using the native package manager (dpkg on Debian/Ubuntu, rpm on RHEL/Fedora).
#
# Requires root (or sudo).  Override DESTDIR for staged installs.
#
DESTDIR       ?=
INSTALL_LIB   ?= $(DESTDIR)/usr/local/lib
INSTALL_INC   ?= $(DESTDIR)/usr/local/include/staticdb2

install: lib db2delta
	@# ── libstaticdb2: headers + archive ──────────────────────────────────
	install -d $(INSTALL_LIB) $(INSTALL_INC)
	install -m 0644 libstaticdb2.a          $(INSTALL_LIB)/libstaticdb2.a
	install -m 0644 include/staticdb2/sdb2.h $(INSTALL_INC)/sdb2.h
	install -m 0644 include/staticdb2/sql.h  $(INSTALL_INC)/sql.h
	@echo "Installed libstaticdb2.a → $(INSTALL_LIB)"
	@echo "Installed headers       → $(INSTALL_INC)"
	@# ── db2delta: detect package manager and install ──────────────────────
	$(MAKE) -C db2delta package
	@if command -v dpkg >/dev/null 2>&1; then \
	    pkg=$$(find db2delta/packaging -maxdepth 1 -name '*.deb' | head -1); \
	    echo "Installing $$pkg via dpkg ..."; \
	    dpkg -i $$pkg; \
	elif command -v rpm >/dev/null 2>&1; then \
	    pkg=$$(find db2delta/packaging/rpmbuild/RPMS -name '*.rpm' | head -1); \
	    echo "Installing $$pkg via rpm ..."; \
	    rpm -Uvh $$pkg; \
	else \
	    echo "WARNING: no dpkg or rpm found — installing db2delta binary manually"; \
	    install -d $(DESTDIR)/usr/local/bin; \
	    install -m 0755 db2delta/db2delta $(DESTDIR)/usr/local/bin/db2delta; \
	    install -d $(DESTDIR)/etc/bash_completion.d; \
	    install -m 0644 db2delta/completions/db2delta.bash \
	        $(DESTDIR)/etc/bash_completion.d/db2delta; \
	fi

