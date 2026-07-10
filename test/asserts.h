#pragma GCC diagnostic ignored "-Wunused-function"

#ifndef ASSERTS_H
#define ASSERTS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#undef assert

/* ANSI color codes */
#define COLOR_GREEN  "\033[0;32m"
#define COLOR_RED    "\033[0;31m"
#define COLOR_RESET  "\033[0m"

static int assert_pass = 0;
static int assert_fail = 0;

static void assert(const char* msg, int thing)
{
    if (thing) {
        printf("  " COLOR_GREEN "PASS" COLOR_RESET "  %s\n", msg);
        assert_pass++;
    } else {
        fprintf(stderr, "  " COLOR_RED "FAIL" COLOR_RESET "  %s\n", msg);
        assert_fail++;
    }
}

/*
 * assert_int_equals - check if two integers are equal
 */
static void assert_int_equals(const char* msg, int actual, int expected)
{
    if (actual == expected) {
        printf("  " COLOR_GREEN "PASS" COLOR_RESET "  %s (got %d)\n", msg, actual);
        assert_pass++;
    } else {
        fprintf(stderr, "  " COLOR_RED "FAIL" COLOR_RESET "  %s: expected %d, got %d\n", msg, expected, actual);
        assert_fail++;
    }
}

/*
 * assert_long_equals - check if two long integers are equal
 */
static void assert_long_equals(const char* msg, long actual, long expected)
{
    if (actual == expected) {
        printf("  " COLOR_GREEN "PASS" COLOR_RESET "  %s (got %ld)\n", msg, actual);
        assert_pass++;
    } else {
        fprintf(stderr, "  " COLOR_RED "FAIL" COLOR_RESET "  %s: expected %ld, got %ld\n", msg, expected, actual);
        assert_fail++;
    }
}

/*
 * assert_long_le - check that actual <= max (useful for performance targets)
 */
static void assert_long_le(const char* msg, long actual, long max)
{
    if (actual <= max) {
        printf("  " COLOR_GREEN "PASS" COLOR_RESET "  %s: %ld <= %ld\n", msg, actual, max);
        assert_pass++;
    } else {
        fprintf(stderr, "  " COLOR_RED "FAIL" COLOR_RESET "  %s: %ld > %ld\n", msg, actual, max);
        assert_fail++;
    }
}

/*
 * assert_string_equals - check if two strings are equal
 */
static void assert_string_equals(const char* msg, const char* actual, const char* expected)
{
    if (actual == NULL && expected == NULL) {
        printf("  " COLOR_GREEN "PASS" COLOR_RESET "  %s (both NULL)\n", msg);
        assert_pass++;
    } else if (actual == NULL || expected == NULL) {
        fprintf(stderr, "  " COLOR_RED "FAIL" COLOR_RESET "  %s: one is NULL (actual=%p, expected=%p)\n", msg, (void*)actual, (void*)expected);
        assert_fail++;
    } else if (strcmp(actual, expected) == 0) {
        printf("  " COLOR_GREEN "PASS" COLOR_RESET "  %s\n", msg);
        assert_pass++;
    } else {
        fprintf(stderr, "  " COLOR_RED "FAIL" COLOR_RESET "  %s:\n", msg);
        fprintf(stderr, "        expected: \"%s\"\n", expected);
        fprintf(stderr, "        got:      \"%s\"\n", actual);
        assert_fail++;
    }
}

static void section(const char *label)
{
    printf("\n=== %s ===\n", label);
}

static void log_failures(const char * test_name)
{
    FILE *fd = fopen("/tmp/results.log", "a");
    fprintf(fd, "FAIL – %s test(s) failed.\n", test_name);
    fclose(fd);
}

static void print_results(const char * test_name)
{
    printf("\n--- Results ---\n");
    printf("  passed : %d\n", assert_pass);
    printf("  failed : %d\n", assert_fail);
    printf("\n");
    if (assert_fail > 0) {
        fprintf(stderr, "FAIL – %d test(s) failed.\n", assert_fail);
        log_failures(test_name);
        exit(1);
    }
    printf("OK – all %d tests passed.\n", assert_pass);
}


/*
 * LONG_TEST(name, body)
 *
 * Wraps a long-running snip test body.  The code is always compiled but only
 * executed at runtime when the LONG_TESTS environment variable is set to "1".
 * Otherwise a SKIP line is printed and the body is not entered.
 *
 * Usage:
 *   LONG_TEST("backoff reaches max at iteration 32", {
 *       for (int i = 0; i < 40; i++) ngx_http_mx_backoff_delay(&bo);
 *       assert_int_equals("capped at max", (int)ngx_http_mx_backoff_delay(&bo), 15000);
 *   });
 */
#define LONG_TEST(name, body)                                               do {                                                                        const char *_lt = getenv("LONG_TESTS");                                 if (_lt != NULL && _lt[0] == '1' && _lt[1] == '\0') {                      body                                                                } else {                                                                    printf("  \033[0;33mSKIP\033[0m  %s (set LONG_TESTS=1 to run)\n",                    name);                                                       }                                                                   } while (0)

/*
 * assert_require(msg, condition, body)
 *
 * If condition is true: prints PASS and executes body.
 * If condition is false: prints FAIL and skips body — subsequent tests
 * that would segfault on a bad precondition are never reached.
 *
 *   assert_require("pool created", pool != NULL, {
 *       assert("has elements", pool->count > 0);
 *   });
 */
#define assert_require(msg, condition, body)                                do {                                                                        int _cond = (int)(condition); /* consume expression */                  (void)_cond;                                                            if (_cond) {                                                                assert_pass++;                                                          do { body } while (0);                                              } else {                                                                    fprintf(stderr, "  " COLOR_RED "FAIL" COLOR_RESET                              "  %s (prerequisite failed — skipping dependent tests)\n", msg);             assert_fail++;                                                      }                                                                   } while (0)

/*
 * assert_require_int_equals(msg, actual, expected, body)
 *
 * Evaluates `actual` inside the macro so the caller's variable is always
 * consumed — no unused-variable warning even when body is skipped.
 * If actual == expected: prints PASS and executes body.
 * If not: prints FAIL with both values and skips body.
 *
 *   assert_require_int_equals("parse ok", rc, 0, {
 *       assert("field valid", result.field == 42);
 *   });
 */
#define assert_require_int_equals(msg, actual, expected, body)              do {                                                                        int _a = (int)(actual);  /* consume caller's variable here */           int _e = (int)(expected);                                               (void)_a; (void)_e;                                                     if (_a == _e) {                                                             assert_pass++;                                                          do { body } while (0);                                              } else {                                                                    fprintf(stderr, "  " COLOR_RED "FAIL" COLOR_RESET                               "  %s: expected %d, got %d"                                             " (prerequisite failed — skipping dependent tests)\n",                     msg, _e, _a);                                                   assert_fail++;                                                      }                                                                   } while (0)

/* Compatibility macros — map old CHECK/CHECK_EQ style to asserts.h */
#define CHECK(expr) assert(#expr, (expr))
#define CHECK_EQ(a, b) assert_int_equals(#a " == " #b, (int)(a), (int)(b))

#endif // ASSERTS_H
