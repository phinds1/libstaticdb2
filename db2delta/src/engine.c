#include "engine.h"
#include "changelog.h"
#include "scan.h"
#include "sha256.h"
#include "../../include/staticdb2/sql.h"
#include "../../include/staticdb2/sdb2.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── pure decision logic ─────────────────────────────────────────────────── */

engine_decision_t engine_decide(int already_present, int stmt_failed,
                                 int continue_on_error) {
    if (already_present)  return ENG_SKIP_PRESENT;
    if (!stmt_failed)     return ENG_APPLY_RECORD;
    return continue_on_error ? ENG_CONTINUE_FAIL : ENG_STOP_FAIL;
}

/* ── canonical hash of a changeset ──────────────────────────────────────── */

/* Canonical form: statements joined by '\n', trimmed.
 * This is the string that gets SHA-256 hashed and stored in the changelog. */
static void changeset_canonical(const delta_section_t *sec,
                                 char *out, size_t out_sz) {
    size_t pos = 0;
    for (int i = 0; i < sec->stmt_count; i++) {
        if (i > 0 && pos + 1 < out_sz) out[pos++] = '\n';
        size_t slen = strlen(sec->stmts[i]);
        if (pos + slen >= out_sz) slen = out_sz - pos - 1;
        memcpy(out + pos, sec->stmts[i], slen);
        pos += slen;
    }
    out[pos] = '\0';
}

/* ── execute one changeset ───────────────────────────────────────────────── */

static int run_changeset(db2delta_conn_t *c, const delta_section_t *sec,
                          int continue_on_error) {
    int any_fail = 0;
    for (int i = 0; i < sec->stmt_count; i++) {
        if (!sec->stmts[i] || !*sec->stmts[i]) continue;
        int r = conn_exec(c, sec->stmts[i]);
        if (r != 0) {
            fprintf(stderr, "  error in changeset '%s', statement %d\n",
                    sec->name, i + 1);
            any_fail = 1;
            if (!continue_on_error) break;
        }
    }
    return any_fail ? -1 : 0;
}

/* ── engine_query: exec subcommand arbitrary SQL ─────────────────────────── */

int engine_query(db2delta_conn_t *c, const char *sql, int script_mode) {
    SQLHSTMT hstmt = SQL_NULL_HANDLE;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT, c->hdbc, &hstmt);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;

    rc = SQLExecDirect(hstmt, (SQLCHAR *)sql, SQL_NTS);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
        conn_print_diag(SQL_HANDLE_STMT, hstmt, sql);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return -1;
    }

    SQLSMALLINT ncols = 0;
    SQLNumResultCols(hstmt, &ncols);

    if (ncols == 0) {
        /* DML / DDL — print row count */
        SQLLEN nrows = 0;
        SQLRowCount(hstmt, &nrows);
        if (!script_mode) printf("%d row(s) affected\n", (int)nrows);
    } else {
        /* Result set — fetch and print rows */
        char col_buf[4096];
        SQLLEN col_ind;
        while ((rc = SQLFetch(hstmt)) == SQL_SUCCESS ||
                rc == SQL_SUCCESS_WITH_INFO) {
            for (SQLSMALLINT i = 1; i <= ncols; i++) {
                if (i > 1) printf("\t");
                col_buf[0] = '\0'; col_ind = 0;
                SQLGetData(hstmt, (SQLUSMALLINT)i, SQL_C_CHAR,
                           col_buf, sizeof(col_buf), &col_ind);
                if (col_ind == SQL_NULL_DATA)
                    printf("NULL");
                else
                    printf("%s", col_buf);
            }
            printf("\n");
        }
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return 0;
}

/* ── engine_apply: main loop ─────────────────────────────────────────────── */

int engine_apply(db2delta_conn_t *c, const db2delta_args_t *args,
                 engine_result_t *result) {
    memset(result, 0, sizeof(*result));

    /* Ensure changelog table exists */
    if (changelog_ensure(c) != 0) {
        fprintf(stderr, "error: failed to ensure DBDELTA_CHANGELOG\n");
        return -1;
    }

    /* Build ordered file list */
    char **paths = NULL; int path_count = 0;
    if (scan_resolve((char **)args->sql_files, args->sql_file_count,
                     &paths, &path_count) != 0) {
        fprintf(stderr, "error: failed to resolve delta files\n");
        return -1;
    }

    int stop = 0;
    for (int fi = 0; fi < path_count && !stop; fi++) {
        delta_file_t df;
        if (delta_parse_file(paths[fi], &df) != 0) {
            fprintf(stderr, "error: failed to parse '%s'\n", paths[fi]);
            if (!args->continue_on_error) { stop = 1; break; }
            result->failed++;
            continue;
        }

        /* File-level continue-on-error overrides CLI if set to true */
        int cont = args->continue_on_error || df.continue_on_error;

        for (int si = 0; si < df.section_count && !stop; si++) {
            const delta_section_t *sec = &df.sections[si];
            if (sec->kind != DELTA_CHANGESET) continue;

            int present = 0;
            if (changelog_has(c, sec->name, &present) != 0) {
                fprintf(stderr, "error: changelog_has failed for '%s'\n",
                        sec->name);
                if (!cont) { stop = 1; result->failed++; break; }
                result->failed++;
                continue;
            }

            /* Compute canonical hash */
            char canonical[65536];
            changeset_canonical(sec, canonical, sizeof(canonical));
            char sha[65];
            sha256_hex((const uint8_t *)canonical, strlen(canonical), sha);

            engine_decision_t dec = engine_decide(present, 0, cont);
            if (dec == ENG_SKIP_PRESENT) {
                if (!args->script_mode)
                    printf("  SKIP  [%s] %s (already applied)\n",
                           paths[fi], sec->name);
                result->skipped++;
                continue;
            }

            /* Execute statements */
            if (!args->script_mode)
                printf("  APPLY [%s] %s\n", paths[fi], sec->name);

            int exec_fail = run_changeset(c, sec, cont);
            dec = engine_decide(present, exec_fail != 0, cont);

            if (dec == ENG_APPLY_RECORD) {
                changelog_record(c, sec->name, paths[fi], sha,
                                 df.author[0] ? df.author : NULL);
                result->applied++;
                if (!args->script_mode)
                    printf("         → recorded (sha=%.*s...)\n", 8, sha);

                /* Run any immediately-following VALIDATION sections */
                for (int vi = si + 1;
                     vi < df.section_count &&
                     df.sections[vi].kind == DELTA_VALIDATION;
                     vi++) {
                    const delta_section_t *vsec = &df.sections[vi];
                    int vpass = 0;
                    if (!args->script_mode)
                        printf("  VALID [%s] %s\n", paths[fi], vsec->name);
                    if (engine_validate(c, vsec, &vpass) != 0 || !vpass) {
                        fprintf(stderr, "  VALIDATION FAILED: %s\n",
                                vsec->name);
                        result->failed++;
                        if (!cont) { stop = 1; break; }
                    } else if (!args->script_mode) {
                        printf("         → PASS\n");
                    }
                }
            } else if (dec == ENG_STOP_FAIL) {
                fprintf(stderr, "  FAIL  [%s] %s — aborting\n",
                        paths[fi], sec->name);
                result->failed++;
                stop = 1;
            } else { /* ENG_CONTINUE_FAIL */
                fprintf(stderr, "  FAIL  [%s] %s — continuing\n",
                        paths[fi], sec->name);
                result->failed++;
            }
        }
        delta_free(&df);
    }

    scan_free(paths, path_count);
    return stop ? -1 : 0;
}

/* ── validation_passes: pure, unit-testable ─────────────────────────────── */

int validation_passes(const sdb2_value_t *first_col, int had_row) {
    if (!had_row || !first_col) return 0;
    if (first_col->is_null) return 0;

    switch (first_col->type) {
    case SDB2_C_INT16:  return first_col->v.i16 != 0;
    case SDB2_C_INT32:  return first_col->v.i32 != 0;
    case SDB2_C_INT64:  return first_col->v.i64 != 0;
    case SDB2_C_FLOAT32: return first_col->v.f32 != 0.0f;
    case SDB2_C_FLOAT64: return first_col->v.f64 != 0.0;
    case SDB2_C_BOOL:   return first_col->v.b != 0;
    case SDB2_C_CHAR:
    case SDB2_C_VARCHAR:
    case SDB2_C_LONG_VARCHAR:
    case SDB2_C_DATE:
    case SDB2_C_TIME:
    case SDB2_C_TIMESTAMP:
    case SDB2_C_DECIMAL:
        return first_col->v.str.len > 0 && first_col->v.str.data[0] != '\0';
    default:
        return 1; /* unknown type — assume pass */
    }
}

/* ── engine_validate ────────────────────────────────────────────────────── */

int engine_validate(db2delta_conn_t *c, const delta_section_t *sec,
                    int *out_pass) {
    *out_pass = 0;
    if (sec->stmt_count == 0) { *out_pass = 1; return 0; } /* vacuously true */

    SQLHSTMT hstmt = SQL_NULL_HANDLE;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT, c->hdbc, &hstmt);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;

    /* Run first statement only for validation */
    rc = SQLExecDirect(hstmt, (SQLCHAR *)sec->stmts[0], SQL_NTS);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
        conn_print_diag(SQL_HANDLE_STMT, hstmt, sec->name);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return -1;
    }

    char col_buf[512] = {0};
    SQLLEN col_ind = 0;
    if (SQLBindCol(hstmt, 1, SQL_C_CHAR, col_buf, sizeof(col_buf), &col_ind)
            != SQL_SUCCESS) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return -1;
    }

    int had_row = 0;
    rc = SQLFetch(hstmt);
    if (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO) {
        had_row = 1;
    }

    /* Build a minimal sdb2_value_t for the pure check */
    sdb2_value_t val;
    memset(&val, 0, sizeof(val));
    if (had_row && col_ind != SQL_NULL_DATA) {
        val.type = SDB2_C_VARCHAR;
        int n = (int)col_ind;
        if (n < 0) n = 0;
        if (n >= (int)sizeof(val.v.str.data))
            n = (int)sizeof(val.v.str.data) - 1;
        memcpy(val.v.str.data, col_buf, (size_t)n);
        val.v.str.data[n] = '\0';
        val.v.str.len = n;
    } else if (had_row) {
        val.is_null = 1;
        val.type = SDB2_C_VARCHAR;
    }

    *out_pass = validation_passes(had_row ? &val : NULL, had_row);
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return 0;
}

