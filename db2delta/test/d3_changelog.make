#!/bin/bash
# Phase 3 — changelog SQL builder snip test
set -e

musl-gcc -std=c11 -Wall -Wextra -I ../../include -I ../src -static \
    -o run_d3_changelog -x c d3_changelog \
    ../src/args.c ../src/conn.c ../src/changelog.c \
    -x none ../../libstaticdb2.a -lm

./run_d3_changelog

rm -f run_d3_changelog
