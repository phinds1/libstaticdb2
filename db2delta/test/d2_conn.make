#!/bin/bash
# Phase 2 — db2delta conn snip test
set -e

musl-gcc -std=c11 -Wall -Wextra -I ../../include -I ../src -static \
    -o run_d2_conn -x c d2_conn \
    ../src/args.c ../src/conn.c \
    -x none ../../libstaticdb2.a -lm

./run_d2_conn

rm -f run_d2_conn
