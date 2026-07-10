CC      = musl-gcc
CFLAGS  = -std=c11 -Wall -Wextra -I ../include -I src -static \
          -DDB2DELTA_VERSION=\"$(shell cat version)\"
LDFLAGS = ../libstaticdb2.a -lm

SNIPRUN = ../snip/sniprun

SRC = src/main.c src/args.c src/conn.c src/changelog.c src/delta.c src/scan.c src/sha256.c src/engine.c

CPPCHECK = cppcheck
CPPCHECK_FLAGS = --enable=warning,style,performance --std=c11 \
                 --suppress=missingIncludeSystem \
                 --suppress=checkersReport \
                 -I ../include -I src \
                 --error-exitcode=1 --quiet

.PHONY: all test lint clean rpm deb package

all: db2delta

db2delta: $(SRC)
	$(CC) $(CFLAGS) -o db2delta $(SRC) -x none $(LDFLAGS)

test:
	$(SNIPRUN) test/d1_args.c.snip
	$(SNIPRUN) test/d2_conn.c.snip
	$(SNIPRUN) test/d3_changelog.c.snip
	$(SNIPRUN) test/d4_delta.c.snip
	$(SNIPRUN) test/d5_scan.c.snip
	$(SNIPRUN) test/d6_sha256.c.snip
	$(SNIPRUN) test/d7_engine.c.snip
	$(SNIPRUN) test/d8_validation.c.snip
	$(SNIPRUN) test/d9_env.c.snip
# d_integration skipped in unit test run — needs DB2_TEST_HOST
# Run via: local-integration/tests/run-db2delta-integration.sh

lint:
	$(CPPCHECK) $(CPPCHECK_FLAGS) $(SRC)

clean:
	rm -f db2delta
	rm -f test/d1_args test/d1_args.c test/run_d1_args
	rm -f test/d2_conn test/d2_conn.c test/run_d2_conn
	rm -f test/d3_changelog test/d3_changelog.c test/run_d3_changelog
	rm -f test/d4_delta test/d4_delta.c test/run_d4_delta
	rm -f test/d5_scan test/d5_scan.c test/run_d5_scan
	rm -f test/d6_sha256 test/d6_sha256.c test/run_d6_sha256
	rm -f test/d7_engine test/d7_engine.c test/run_d7_engine
	rm -f test/d8_validation test/d8_validation.c test/run_d8_validation
	rm -f test/d9_env test/d9_env.c test/run_d9_env
	rm -f test/d_integration test/d_integration.c test/run_d_integration
	rm -rf packaging/rpmbuild packaging/db2delta_*

# ── Packaging ─────────────────────────────────────────────────────────────────
VERSION  := $(shell cat version)
DEB_ARCH := $(shell dpkg --print-architecture 2>/dev/null || echo amd64)
RPM_TOPDIR := $(CURDIR)/packaging/rpmbuild

rpm: all
	@mkdir -p $(RPM_TOPDIR)/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
	rpmbuild \
	  --define "_topdir $(RPM_TOPDIR)" \
	  --define "version $(VERSION)" \
	  --define "srcdir $(CURDIR)" \
	  -bb packaging/db2delta.spec
	@echo "RPM: $$(find $(RPM_TOPDIR)/RPMS -name '*.rpm')"

deb: all
	$(eval STAGE := packaging/db2delta_$(VERSION)_$(DEB_ARCH))
	@rm -rf $(STAGE)
	@mkdir -p $(STAGE)/usr/bin $(STAGE)/etc/bash_completion.d $(STAGE)/DEBIAN
	install -m 0755 db2delta              $(STAGE)/usr/bin/db2delta
	install -m 0644 completions/db2delta.bash \
	                $(STAGE)/etc/bash_completion.d/db2delta
	sed 's/@VERSION@/$(VERSION)/g; s/@ARCH@/$(DEB_ARCH)/g' \
	  packaging/deb/DEBIAN/control.in > $(STAGE)/DEBIAN/control
	dpkg-deb --build $(STAGE) packaging/db2delta_$(VERSION)_$(DEB_ARCH).deb
	@rm -rf $(STAGE)
	@echo "DEB: packaging/db2delta_$(VERSION)_$(DEB_ARCH).deb"

package: rpm deb
