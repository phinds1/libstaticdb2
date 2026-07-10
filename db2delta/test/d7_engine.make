#!/bin/bash
# Phase 7 — engine decision snip test
set -e

musl-gcc -std=c11 -Wall -Wextra -I ../../include -I ../src -static \
    -o run_d7_engine -x c d7_engine \
    ../src/args.c ../src/conn.c ../src/changelog.c \
    ../src/delta.c ../src/scan.c ../src/sha256.c ../src/engine.c \
    -x none ../../libstaticdb2.a -lm

./run_d7_engine

rm -f run_d7_engine
