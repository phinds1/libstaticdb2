---
name: perf-testing
description: write snip performance tests that output .perf files and validate against .perf.target baselines
allowed-tools: bash, snip/sniprun, chmod
---

# Performance testing with snip

Performance tests follow the same snip framework as unit tests but measure throughput or latency and write results to `target/<test-name>.perf`.

Once a baseline is established the developer copies the `.perf` file to `test/snip/<module>/test-<test-name>.perf.target`.
From that point on, every run validates that performance has not regressed beyond the recorded targets.

N.B. **It is forbidden for agents to create their own `.perf.target` files, humans must set the targets.**
**Equally, it is forbidden for agents to change targets, ever**

## File naming

| File                                | Purpose                                      |
|-------------------------------------|----------------------------------------------|
| `test/perf/<name>_perf.c.snip`      | Snip source — the timed test                 |
| `test/perf/<name>_perf.c.make`      | Build + run script (chmod +x)                |
| `target/test-<name>.perf`           | Generated result file (gitignored)           |
| `test/perf/test-<name>.perf.target` | Committed baseline — enables regression gate |

The `target/` directory is created automatically by the test. The `.perf.target` file is committed by the developer only when happy with the baseline.

## .perf file format

Key=value pairs, one per line. Lines starting with `#` are comments.

```
# description of what was measured
iters=10000000
total_ns=9800000
avg_ns=0
```

Use keys that make sense for the metric. Common keys:

- `avg_ns` — average nanoseconds per operation (use when > 0 after integer division)
- `total_ns` or `total_us` — total wall time (use when avg rounds to zero — very fast operations)
- `records`, `lookups` — counts for context

**Always include `total_ns` or `total_us` as the regression key when timing CPU-bound operations** — `avg_ns` rounds to zero for sub-nanosecond amortised operations that the compiler has vectorised.

## write_perf_file helper

```c
static void write_perf_file(const char *path, long total_ns, long avg_ns)
{
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; mkdir(dir, 0755); }

    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "write_perf_file: %s: %s\n", path, strerror(errno)); return; }
    fprintf(f, "# <module> performance results\n");
    fprintf(f, "iters=%d\n",      ITERS);
    fprintf(f, "total_ns=%ld\n",  total_ns);
    fprintf(f, "avg_ns=%ld\n",    avg_ns);
    fclose(f);
    printf("  perf results → %s\n", path);
}
```

## parse_perf_key / check_targets helpers

```c
static long parse_perf_key(FILE *f, const char *key)
{
    rewind(f);
    char line[256];
    size_t klen = strlen(key);
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        if (strncmp(line, key, klen) == 0 && line[klen] == '=') return atol(line + klen + 1);
    }
    return -1;
}

static void check_targets(const char *target_path, long total_ns, long avg_ns)
{
    FILE *f = fopen(target_path, "r");
    if (!f) return;   /* no target file — first run, nothing to check */

    printf("\n  Checking against targets in %s\n", target_path);
    long t_total = parse_perf_key(f, "total_ns");
    long t_avg   = parse_perf_key(f, "avg_ns");
    fclose(f);

    if (t_total > 0) assert_long_le("total_ns", total_ns, t_total);
    if (t_avg   > 0) assert_long_le("avg_ns",   avg_ns,   t_avg);
}
```

`check_targets` is a no-op when the `.perf.target` file does not exist — safe to run before a baseline is set.

## Timing loop pattern

Use `clock_gettime(CLOCK_MONOTONIC)`. Use enough iterations that the total wall time is at least a few milliseconds to reduce OS scheduling noise.

Prevent dead-code elimination with an XOR accumulator fed into a `volatile` sink variable at the end of the loop. Use a cycling input (LCG or incrementing counter) to prevent constant folding.

```c
#define ITERS 10000000

uint32_t v   = 0xDEADBEEFu;
uint8_t  acc = 0;

struct timespec t0, t1;
clock_gettime(CLOCK_MONOTONIC, &t0);
for (int i = 0; i < ITERS; i++) {
    uint8_t out[4];
    function_under_test(out, v);
    v   = v * 1664525u + 1013904223u;   /* LCG */
    acc ^= out[0] ^ out[1] ^ out[2] ^ out[3];
}
clock_gettime(CLOCK_MONOTONIC, &t1);
volatile uint8_t sink = acc;
(void)sink;

long total_ns = ns_elapsed(&t0, &t1);
long avg_ns   = total_ns / ITERS;
printf("  total: %ld ns   avg: %ld ns/call\n", total_ns, avg_ns);
```

Standard ns_elapsed helper:

```c
static long ns_elapsed(struct timespec *s, struct timespec *e)
{
    return (e->tv_sec - s->tv_sec) * 1000000000L + (e->tv_nsec - s->tv_nsec);
}
```

## Typical .c.make for a perf test

```bash
#!/bin/bash
set -euo pipefail

cd $(dirname $0)

snip_test=mymodule_perf

musl-gcc -Wall -Werror -Wno-unused-function -Wno-sign-compare -g -O2 \
    $snip_test.c \
    -o $snip_test \
    -lm \
    && ./$snip_test \
    && rm -f ./$snip_test ./${snip_test}.c
```

Compile with `-O2` to match production build conditions. Do **not** add `-fno-tree-vectorize` — let the compiler do what it would do in production. 
If the compiler optimises both paths to the same speed, that is useful information (no manual optimisation is needed).

## Two-pass perf test (comparing two implementations)

When comparing two implementations (e.g. C reference vs optimised variant) compile the same source twice with different flags, write to separate `.perf` files, and check against separate `.perf.target` files.

```bash
#!/bin/bash
set -euo pipefail

cd $(dirname $0)

snip_test=mymodule_variant_perf

echo "--- pass 1: baseline ---"
musl-gcc -Wall -Werror -Wno-unused-function -Wno-sign-compare -g -O2 \
    $snip_test.c \
    -o ${snip_test}_base \
    && ./${snip_test}_base
rm -f ./${snip_test}_base

echo "--- pass 2: optimised variant ---"
musl-gcc -Wall -Werror -Wno-unused-function -Wno-sign-compare -g -O2 -DVARIANT_FLAG \
    $snip_test.c \
    -o ${snip_test}_opt \
    && ./${snip_test}_opt
rm -f ./${snip_test}_opt ./${snip_test}.c
```

Inside the `.c.snip` use `#ifdef VARIANT_FLAG` to select which `.perf` path and `.perf.target` path to write/read:

```c
#ifdef VARIANT_FLAG
static const char *perf_out    = "../../../target/test-mymodule_variant_opt.perf";
static const char *perf_target = "./test-mymodule_variant_opt.perf.target";
#else
static const char *perf_out    = "../../../target/test-mymodule_variant_base.perf";
static const char *perf_target = "./test-mymodule_variant_base.perf.target";
#endif
```

**Important lesson**: before writing a manual optimisation, run this two-pass test first. If the compiler produces equivalent timings for both passes then the manual optimisation adds complexity for no gain and should not be committed.

## Setting a baseline

After running a perf test for the first time, examine `target/<test>.perf` and set the target to a generous multiple (2×–3×) of the measured value to tolerate system load variation:

```bash
# Run once to generate target/<test>.perf
snip/sniprun test/pef/<name>_perf.c.snip

# Review the numbers — set targets at ~2× measured
# Edit the target file to round numbers with headroom
cp target/test-<name>.perf test/perf/test-<name>.perf.target
# edit test-<name>.perf.target: multiply timing values by ~2
```
