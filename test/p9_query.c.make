#!/bin/bash
# Phase 9 snip test build + run for .c.snip input.
set -euo pipefail

cd $(dirname $0)

snip_test=p9_query

musl-gcc -std=c11 -Wall -Wextra -I ../include -I ../src -static \
    -o run_$snip_test $snip_test.c \
    ../src/handles.c \
    ../src/fdoca.c \
    ../src/sqlca.c \
    ../src/drda.c \
    ../src/dss.c \
    ../src/ddm.c \
    ../src/ccsid.c \
    ../src/buf.c \
    ../src/net.c \
    && ./run_$snip_test \
    && rm -f ./run_$snip_test ./${snip_test}.c
