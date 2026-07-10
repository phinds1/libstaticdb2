#!/bin/bash
set -euo pipefail

cd $(dirname $0)

snip_test=d5_scan

musl-gcc -std=c11 -Wall -Wextra -I ../../include -I ../src -static \
    -DDB2DELTA_VERSION=\"$(cat ../version)\" \
    -o run_$snip_test $snip_test.c \
    ../src/scan.c -lm \
    && ./run_$snip_test \
    && rm -f ./run_$snip_test ./${snip_test}.c
