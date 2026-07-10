#!/bin/bash
set -euo pipefail

cd $(dirname $0)

snip_test=d8_validation

musl-gcc -std=c11 -Wall -Wextra -I ../../include -I ../src -static \
    -DDB2DELTA_VERSION=\"$(cat ../version)\" \
    -o run_$snip_test $snip_test.c \
    ../src/args.c ../src/conn.c ../src/changelog.c ../src/delta.c ../src/scan.c ../src/sha256.c ../src/engine.c -x none ../../target/libstaticdb2.a -lm \
    && ./run_$snip_test \
    && rm -f ./run_$snip_test ./${snip_test}.c
