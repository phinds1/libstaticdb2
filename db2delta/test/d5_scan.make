#!/bin/bash
# Phase 5 — scan snip test
set -e

musl-gcc -std=c11 -Wall -Wextra -I ../../include -I ../src -static \
    -o run_d5_scan -x c d5_scan \
    ../src/scan.c \
    -lm

./run_d5_scan

rm -f run_d5_scan
