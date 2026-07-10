#!/bin/bash
set -euo pipefail

cd $(dirname $0)

snip_test=d6_sha256

musl-gcc -std=c11 -Wall -Wextra -I ../../include -I ../src -static \
    -DDB2DELTA_VERSION=\"$(cat ../version)\" \
    -o run_$snip_test $snip_test.c \
    ../src/sha256.c -lm \
    && ./run_$snip_test \
    && rm -f ./run_$snip_test ./${snip_test}.c
