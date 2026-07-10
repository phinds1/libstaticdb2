#!/bin/bash
# db2delta integration snip test
set -e

musl-gcc -std=c11 -Wall -Wextra -I ../../include -I ../src -static \
    -o run_d_integration -x c d_integration \
    ../src/args.c ../src/conn.c ../src/changelog.c \
    ../src/delta.c ../src/scan.c ../src/sha256.c ../src/engine.c \
    -x none ../../libstaticdb2.a -lm

./run_d_integration "$@"

rm -f run_d_integration
