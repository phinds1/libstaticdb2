#!/bin/bash
# Phase 4 — delta file parser snip test
set -e

musl-gcc -std=c11 -Wall -Wextra -I ../../include -I ../src -static \
    -o run_d4_delta -x c d4_delta \
    ../src/delta.c \
    -x none ../../libstaticdb2.a -lm

./run_d4_delta

rm -f run_d4_delta
