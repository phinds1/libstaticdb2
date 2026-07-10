#!/bin/bash
# Phase 3 snip test build + run for .c.snip input.
set -euo pipefail

cd $(dirname $0)

snip_test=p3_dss

musl-gcc -std=c11 -Wall -Wextra -I ../include -I ../src -static \
    -o run_$snip_test $snip_test.c \
    ../src/dss.c \
    ../src/buf.c \
    && ./run_$snip_test \
    && rm -f ./run_$snip_test ./${snip_test}.c
