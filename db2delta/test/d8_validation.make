#!/bin/bash
# Phase 8 — validation snip test
set -e

musl-gcc -std=c11 -Wall -Wextra -I ../../include -I ../src -static \
    -o run_d8_validation -x c d8_validation \
    ../src/args.c ../src/conn.c ../src/changelog.c \
    ../src/delta.c ../src/scan.c ../src/sha256.c ../src/engine.c \
    -x none ../../libstaticdb2.a -lm

./run_d8_validation

rm -f run_d8_validation
