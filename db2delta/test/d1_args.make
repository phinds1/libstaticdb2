#!/bin/bash
# Phase 1 — db2delta args snip test
set -e

musl-gcc -std=c11 -Wall -Wextra -I ../../include -I ../src -static \
    -o run_d1_args -x c d1_args \
    ../src/args.c \
    -lm

./run_d1_args

rm -f run_d1_args
