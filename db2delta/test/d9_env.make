#!/bin/bash
# Phase 9 — database.env + exit code snip test
set -e

musl-gcc -std=c11 -Wall -Wextra -I ../../include -I ../src -static \
    -o run_d9_env -x c d9_env \
    ../src/args.c ../src/conn.c ../src/changelog.c \
    ../src/delta.c ../src/scan.c ../src/sha256.c ../src/engine.c \
    -x none ../../libstaticdb2.a -lm

./run_d9_env

rm -f run_d9_env
