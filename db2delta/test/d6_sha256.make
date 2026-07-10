#!/bin/bash
# Phase 6 — SHA-256 snip test
set -e

musl-gcc -std=c11 -Wall -Wextra -I ../src -static \
    -o run_d6_sha256 -x c d6_sha256 \
    ../src/sha256.c \
    -lm

./run_d6_sha256

rm -f run_d6_sha256
