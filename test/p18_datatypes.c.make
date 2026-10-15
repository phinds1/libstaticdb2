#!/bin/bash
# Phase 18 snip integration test build + run for .c.snip input.
# Requires DB2_TEST_HOST (and optionally PORT/DB/USER/PASS) to be set.
# See local-integration/tests/run-integration.sh for the full runner.
set -euo pipefail

cd $(dirname $0)

snip_test=p18_datatypes

musl-gcc -std=c11 -Wall -Wextra -I ../include -I ../src -static \
    -o run_$snip_test $snip_test.c \
    ../src/api.c \
    ../src/handles.c \
    ../src/fdoca.c \
    ../src/sqlca.c \
    ../src/drda.c \
    ../src/dss.c \
    ../src/ddm.c \
    ../src/ccsid.c \
    ../src/buf.c \
    ../src/net.c \
    ../src/async.c \
    -lm \
    && ./run_$snip_test "$@" \
    && rm -f ./run_$snip_test ./${snip_test}.c
