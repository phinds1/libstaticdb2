CC      = musl-gcc
CFLAGS  = -std=c11 -Wall -Wextra -I include -I src -static
AR      = ar
ARFLAGS = rcs
SNIPRUN = ./snip/sniprun
TARGET  = target

LIB_SRC = src/buf.c src/net.c src/ccsid.c src/dss.c src/ddm.c src/drda.c src/sqlca.c src/fdoca.c src/handles.c src/api.c src/async.c
LIB_OBJ = $(patsubst src/%.c,$(TARGET)/obj/%.o,$(LIB_SRC))

.PHONY: all lib test lint clean check db2delta db2delta-test db2delta-lint install deb rpm

all: lib db2delta

lib: $(TARGET)/libstaticdb2.a

$(TARGET)/libstaticdb2.a: $(LIB_OBJ) | $(TARGET)
	$(AR) $(ARFLAGS) $@ $^

$(TARGET)/obj/%.o: src/%.c | $(TARGET)/obj
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET):
	mkdir -p $(TARGET)

$(TARGET)/obj: | $(TARGET)
	mkdir -p $(TARGET)/obj

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
	rm -rf $(TARGET)
	@# Remove any sniprun test artifacts that failed mid-run
	rm -f test/*.c test/run_p*
	$(MAKE) -C db2delta clean

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
	install -m 0644 $(TARGET)/libstaticdb2.a $(INSTALL_LIB)/libstaticdb2.a
	install -m 0644 include/staticdb2/sdb2.h $(INSTALL_INC)/sdb2.h
	install -m 0644 include/staticdb2/sql.h  $(INSTALL_INC)/sql.h
	@echo "Installed libstaticdb2.a → $(INSTALL_LIB)"
	@echo "Installed headers       → $(INSTALL_INC)"
	@# ── db2delta: detect package manager and install ──────────────────────
	$(MAKE) -C db2delta package
	@if command -v dpkg >/dev/null 2>&1; then \
	    pkg=$$(find db2delta/target -maxdepth 1 -name '*.deb' | head -1); \
	    echo "Installing $$pkg via dpkg ..."; \
	    dpkg -i $$pkg; \
	elif command -v rpm >/dev/null 2>&1; then \
	    pkg=$$(find db2delta/target/rpmbuild/RPMS -name '*.rpm' | head -1); \
	    echo "Installing $$pkg via rpm ..."; \
	    rpm -Uvh $$pkg; \
	else \
	    echo "WARNING: no dpkg or rpm found — installing db2delta binary manually"; \
	    install -d $(DESTDIR)/usr/local/bin; \
	    install -m 0755 db2delta/target/db2delta $(DESTDIR)/usr/local/bin/db2delta; \
	    install -d $(DESTDIR)/etc/bash_completion.d; \
	    install -m 0644 db2delta/completions/db2delta.bash \
	        $(DESTDIR)/etc/bash_completion.d/db2delta; \
	fi

# ── deb / rpm ─────────────────────────────────────────────────────────────────
# Build the db2delta package in one target packaging format for deployment
# on another system (rpm → RedHat/RHEL/Fedora, deb → Debian/Ubuntu).
# Build only — nothing is installed on the build machine.  To build and
# install locally, use `make install`, which picks the native package manager.
#
deb: db2delta
	$(MAKE) -C db2delta deb
	@pkg=$$(find db2delta/target -maxdepth 1 -name '*.deb' | head -1); \
	if [ -z "$$pkg" ]; then echo "ERROR: no .deb found"; exit 1; fi; \
	echo "DEB: $$pkg"

rpm: db2delta
	$(MAKE) -C db2delta rpm
	@pkg=$$(find db2delta/target/rpmbuild/RPMS -name '*.rpm' | head -1); \
	if [ -z "$$pkg" ]; then echo "ERROR: no .rpm found"; exit 1; fi; \
	echo "RPM: $$pkg"

