CC      = musl-gcc
LDFLAGS = ../target/libstaticdb2.a -lm
TARGET  = target
VERSION  := $(shell cat version)
DEB_ARCH := $(shell dpkg --print-architecture 2>/dev/null || echo amd64)

# BUILD_TYPE controls whether packages carry a SNAPSHOT suffix.
# Default: SNAPSHOT (local/CI builds).  Pass BUILD_TYPE=RELEASE for releases.
BUILD_TYPE ?= SNAPSHOT
ifeq ($(BUILD_TYPE),RELEASE)
FULL_VERSION := $(VERSION)
RPM_RELEASE  := 1
else
FULL_VERSION := $(VERSION)-SNAPSHOT
RPM_RELEASE  := SNAPSHOT
endif

CFLAGS  = -std=c11 -Wall -Wextra -I ../include -I src -static \
          -DDB2DELTA_VERSION=\"$(FULL_VERSION)\"

SNIPRUN = ../snip/sniprun

SRC = src/main.c src/args.c src/conn.c src/changelog.c src/delta.c src/scan.c src/sha256.c src/engine.c

CPPCHECK = cppcheck
CPPCHECK_FLAGS = --enable=warning,style,performance --std=c11 \
                 --suppress=missingIncludeSystem \
                 --suppress=checkersReport \
                 -I ../include -I src \
                 --error-exitcode=1 --quiet

.PHONY: all test lint clean rpm deb package man

all: $(TARGET)/db2delta $(TARGET)/db2delta.1.gz

$(TARGET)/db2delta: $(SRC) | $(TARGET)
	$(CC) $(CFLAGS) -o $@ $(SRC) -x none $(LDFLAGS)

$(TARGET)/db2delta.1.gz: man/db2delta.1 | $(TARGET)
	sed 's/@VERSION@/$(FULL_VERSION)/g' man/db2delta.1 | gzip -9 > $@

man: $(TARGET)/db2delta.1.gz

$(TARGET):
	mkdir -p $(TARGET)

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
	rm -rf $(TARGET)
	@# Remove any sniprun test artifacts that failed mid-run
	rm -f test/*.c test/run_d*

# ── Packaging ─────────────────────────────────────────────────────────────────
RPM_TOPDIR := $(CURDIR)/$(TARGET)/rpmbuild

rpm: all
	@mkdir -p $(RPM_TOPDIR)/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
	rpmbuild \
	  --define "_topdir $(RPM_TOPDIR)" \
	  --define "version $(VERSION)" \
	  --define "pkg_release $(RPM_RELEASE)" \
	  --define "srcdir $(CURDIR)/$(TARGET)" \
	  --define "projdir $(CURDIR)" \
	  -bb packaging/db2delta.spec
	@echo "RPM: $$(find $(RPM_TOPDIR)/RPMS -name '*.rpm')"

deb: all
	$(eval STAGE := $(TARGET)/db2delta_$(FULL_VERSION)_$(DEB_ARCH))
	@rm -rf $(STAGE)
	@mkdir -p $(STAGE)/usr/bin $(STAGE)/etc/bash_completion.d \
	          $(STAGE)/usr/share/man/man1 $(STAGE)/DEBIAN
	install -m 0755 $(TARGET)/db2delta       $(STAGE)/usr/bin/db2delta
	install -m 0644 completions/db2delta.bash \
	                $(STAGE)/etc/bash_completion.d/db2delta
	install -m 0644 $(TARGET)/db2delta.1.gz  $(STAGE)/usr/share/man/man1/db2delta.1.gz
	sed 's/@VERSION@/$(FULL_VERSION)/g; s/@ARCH@/$(DEB_ARCH)/g' \
	  packaging/deb/DEBIAN/control.in > $(STAGE)/DEBIAN/control
	dpkg-deb --build $(STAGE) $(TARGET)/db2delta_$(FULL_VERSION)_$(DEB_ARCH).deb
	@rm -rf $(STAGE)
	@echo "DEB: $(TARGET)/db2delta_$(FULL_VERSION)_$(DEB_ARCH).deb"

package: rpm deb
