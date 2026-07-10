/* Phase 16 — Live DB2 integration tests (snip format)
 *
 * Tests require a running DB2 instance.  Set env vars before running:
 *   DB2_TEST_HOST  - hostname or IP  (required)
 *   DB2_TEST_PORT  - port            (default: 50000)
 *   DB2_TEST_DB    - database name   (default: TESTDB)
 *   DB2_TEST_USER  - username        (default: db2inst1)
 *   DB2_TEST_PASS  - password        (default: db2inst1)
 *
 * Run via local-integration/tests/run-integration.sh, or manually:
 *   cd test && ./p16_integration.c.make
 *
 * Test inventory:
 *   I1.  SQLDriverConnect → SQL_SUCCESS
 *   I2.  CREATE TABLE with INTEGER, VARCHAR, DOUBLE, DATE, DECIMAL columns
 *   I3.  INSERT 5 rows with mixed types
 *   I4.  SELECT * ORDER BY id — fetch all 5 rows, verify id sum
 *   I5.  SQLDescribeCol — column names and types round-trip
 *   I6.  SQLBindParameter + prepared INSERT
 *   I7.  Transaction ROLLBACK — row disappears
 *   I8.  Transaction COMMIT  — row persists after reconnect
 *   I9.  NULL column handling — insert NULL, fetch NULL indicator
 *   I10. Large result set (50 rows) — all fetched without error
 *   I11. SQLGetData on VARCHAR column (unbound)
 *   I12. Error propagation — bad SQL → SQL_ERROR, SQLCODE < 0
 *   I13. SQLRowCount after INSERT / UPDATE / DELETE
 *   I14. SQLPrepare + SQLExecute SELECT with WHERE
 *   I15. DROP TABLE + verify row count = 0 from sysibm.sysdummy1
 *   I16. SQLDisconnect
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "staticdb2/sql.h"
#include "staticdb2/sdb2.h"
#include "asserts.h"

static int failed  = 0;
static int passed  = 0;

/* ── helpers ─────────────────────────────────────────────────────────────── */

#undef CHECK
#undef CHECK_EQ

#define CHECK(expr) \
    do { \
        int _ok = !!(expr); \
        if (_ok) { \
            passed++; \
        } else { \
            failed++; \
        } \
        assert(#expr, _ok); \
    } while (0)

#define CHECK_EQ(a, b) \
    do { \
        long _a = (long)(a); \
        long _b = (long)(b); \
        if (_a == _b) { \
            passed++; \
        } else { \
            failed++; \
        } \
        assert_long_equals(#a " == " #b, _a, _b); \
    } while (0)

static void print_diag(SQLSMALLINT htype, SQLHANDLE h, const char *ctx) {
    SQLCHAR state[6] = {0}, msg[512] = {0};
    SQLINTEGER native = 0;
    SQLSMALLINT mlen  = 0;
    SQLGetDiagRec(htype, h, 1, state, &native, msg, sizeof msg, &mlen);
    printf("  [diag %s] state=%.5s native=%d msg=%s\n",
           ctx, state, (int)native, (char *)msg);
}

/* Execute SQL and return the SQLRETURN; print diagnostics on error. */
static SQLRETURN exec(SQLHSTMT s, const char *sql) {
    SQLRETURN rc = SQLExecDirect(s, (SQLCHAR *)sql, SQL_NTS);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
        print_diag(SQL_HANDLE_STMT, s, sql);
    return rc;
}

/* ── test body ───────────────────────────────────────────────────────────── */

static void run_live_tests(const char *host, int port,
                            const char *db, const char *uid,
                            const char *pwd) {
    printf("\n--- Live DB2 integration tests (%s:%d/%s) ---\n", host, port, db);

    SQLHENV henv  = SQL_NULL_HANDLE;
    SQLHDBC hdbc  = SQL_NULL_HANDLE;
    SQLHSTMT hstmt = SQL_NULL_HANDLE;
    SQLRETURN rc;

    CHECK_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv), SQL_SUCCESS);
    CHECK_EQ(SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                           (SQLPOINTER)SQL_OV_ODBC3, 0), SQL_SUCCESS);
    CHECK_EQ(SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc), SQL_SUCCESS);

    /* I1: connect */
    printf("  I1: SQLDriverConnect\n");
    char cs[512];
    snprintf(cs, sizeof cs,
             "HOSTNAME=%s;PORT=%d;DATABASE=%s;UID=%s;PWD=%s",
             host, port, db, uid, pwd);
    rc = SQLDriverConnect(hdbc, NULL,
                          (SQLCHAR *)cs, SQL_NTS,
                          NULL, 0, NULL, SQL_DRIVER_NOPROMPT);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
        print_diag(SQL_HANDLE_DBC, hdbc, "SQLDriverConnect");
        CHECK(0 && "connect failed — aborting integration tests");
        goto cleanup;
    }
    CHECK(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);

    CHECK_EQ(SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt), SQL_SUCCESS);

    /* Drop table from any prior failed run */
    SQLExecDirect(hstmt,
        (SQLCHAR *)"DROP TABLE SDB2P16_TEST", SQL_NTS);
    SQLFreeStmt(hstmt, SQL_CLOSE);

    /* I2: CREATE TABLE */
    printf("  I2: CREATE TABLE (mixed column types)\n");
    rc = exec(hstmt,
        "CREATE TABLE SDB2P16_TEST("
        "  ID        INTEGER        NOT NULL, "
        "  NAME      VARCHAR(64), "
        "  SCORE     DOUBLE, "
        "  BIRTHDATE DATE, "
        "  BALANCE   DECIMAL(10,2)"
        ")");
    CHECK(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);
    SQLFreeStmt(hstmt, SQL_CLOSE);

    /* I3: INSERT 5 rows */
    printf("  I3: INSERT 5 rows\n");
    const char *inserts[] = {
        "INSERT INTO SDB2P16_TEST VALUES(1,'Alice',9.5,'1990-01-15',1234.56)",
        "INSERT INTO SDB2P16_TEST VALUES(2,'Bob',7.2,'1985-06-30',99.99)",
        "INSERT INTO SDB2P16_TEST VALUES(3,'Charlie',8.1,'1992-11-01',5000.00)",
        "INSERT INTO SDB2P16_TEST VALUES(4,'Diana',9.9,'1988-03-22',750.25)",
        "INSERT INTO SDB2P16_TEST VALUES(5,'Eve',6.0,'1995-07-04',0.01)",
    };
    for (int i = 0; i < 5; i++) {
        SQLRETURN ir = exec(hstmt, inserts[i]);
        CHECK(ir == SQL_SUCCESS || ir == SQL_SUCCESS_WITH_INFO);
        SQLLEN nrows = 0;
        SQLRowCount(hstmt, &nrows);
        CHECK_EQ((int)nrows, 1);
        SQLFreeStmt(hstmt, SQL_CLOSE);
    }

    /* I4: SELECT * ORDER BY id — fetch all 5 rows */
    printf("  I4: SELECT * ORDER BY id — fetch + verify id sum\n");
    rc = exec(hstmt,
        "SELECT ID, NAME, SCORE, BIRTHDATE, BALANCE "
        "FROM SDB2P16_TEST ORDER BY ID");
    CHECK(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);

    SQLINTEGER id = 0;
    char name[128] = {0};
    SQLDOUBLE score = 0;
    char birthdate[32] = {0};
    char balance[32] = {0};
    SQLLEN id_ind, name_ind, score_ind, date_ind, bal_ind;

    CHECK_EQ(SQLBindCol(hstmt, 1, SQL_C_LONG,   &id,        sizeof id,        &id_ind),   SQL_SUCCESS);
    CHECK_EQ(SQLBindCol(hstmt, 2, SQL_C_CHAR,   name,       sizeof name,      &name_ind), SQL_SUCCESS);
    CHECK_EQ(SQLBindCol(hstmt, 3, SQL_C_DOUBLE, &score,     sizeof score,     &score_ind),SQL_SUCCESS);
    CHECK_EQ(SQLBindCol(hstmt, 4, SQL_C_CHAR,   birthdate,  sizeof birthdate, &date_ind), SQL_SUCCESS);
    CHECK_EQ(SQLBindCol(hstmt, 5, SQL_C_CHAR,   balance,    sizeof balance,   &bal_ind),  SQL_SUCCESS);

    int row_count = 0, id_sum = 0;
    while ((rc = SQLFetch(hstmt)) == SQL_SUCCESS ||
            rc == SQL_SUCCESS_WITH_INFO) {
        row_count++;
        id_sum += (int)id;
        printf("    row%d: id=%d name=%s score=%.1f date=%s bal=%s\n",
               row_count, (int)id, name, score, birthdate, balance);
    }
    CHECK_EQ(rc, SQL_NO_DATA);
    CHECK_EQ(row_count, 5);
    CHECK_EQ(id_sum, 15);  /* 1+2+3+4+5 */

    CHECK_EQ(SQLFreeStmt(hstmt, SQL_UNBIND), SQL_SUCCESS);
    CHECK_EQ(SQLFreeStmt(hstmt, SQL_CLOSE),  SQL_SUCCESS);

    /* I5: SQLDescribeCol — verify column metadata */
    printf("  I5: SQLDescribeCol — column names and types\n");
    rc = exec(hstmt,
        "SELECT ID, NAME, SCORE FROM SDB2P16_TEST FETCH FIRST 1 ROW ONLY");
    CHECK(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);

    SQLCHAR colname[128] = {0};
    SQLSMALLINT colname_len, data_type, dec_digits, nullable;
    SQLULEN col_size;
    CHECK_EQ(SQLDescribeCol(hstmt, 1, colname, sizeof colname,
                            &colname_len, &data_type, &col_size,
                            &dec_digits, &nullable), SQL_SUCCESS);
    CHECK(colname_len > 0);
    CHECK(data_type == SQL_INTEGER || data_type == SQL_SMALLINT ||
          data_type == SQL_BIGINT);
    printf("    col1: name=%s type=%d\n", colname, (int)data_type);

    CHECK_EQ(SQLDescribeCol(hstmt, 2, colname, sizeof colname,
                            &colname_len, &data_type, &col_size,
                            &dec_digits, &nullable), SQL_SUCCESS);
    CHECK(data_type == SQL_VARCHAR || data_type == SQL_CHAR);
    CHECK(nullable == SQL_NULLABLE || nullable == SQL_NULLABLE_UNKNOWN);
    printf("    col2: name=%s type=%d size=%d nullable=%d\n",
           colname, (int)data_type, (int)col_size, (int)nullable);
    SQLFreeStmt(hstmt, SQL_CLOSE);

    /* I6: SQLBindParameter + prepared INSERT */
    printf("  I6: SQLBindParameter — prepared INSERT\n");
    rc = SQLPrepare(hstmt,
        (SQLCHAR *)"INSERT INTO SDB2P16_TEST(ID, NAME, SCORE) VALUES(?, ?, ?)",
        SQL_NTS);
    CHECK(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);

    SQLINTEGER p_id = 6;
    char p_name[64] = "Frank";
    SQLDOUBLE p_score = 8.8;
    SQLLEN p_id_ind = 0, p_name_ind = SQL_NTS, p_score_ind = 0;

    CHECK_EQ(SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT, SQL_C_LONG,
                               SQL_INTEGER, 10, 0,
                               &p_id, sizeof p_id, &p_id_ind), SQL_SUCCESS);
    CHECK_EQ(SQLBindParameter(hstmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR,
                               SQL_VARCHAR, 64, 0,
                               p_name, sizeof p_name, &p_name_ind), SQL_SUCCESS);
    CHECK_EQ(SQLBindParameter(hstmt, 3, SQL_PARAM_INPUT, SQL_C_DOUBLE,
                               SQL_DOUBLE, 15, 0,
                               &p_score, sizeof p_score, &p_score_ind), SQL_SUCCESS);

    rc = SQLExecute(hstmt);
    CHECK(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);
    SQLLEN nrows = 0;
    CHECK_EQ(SQLRowCount(hstmt, &nrows), SQL_SUCCESS);
    CHECK_EQ((int)nrows, 1);
    SQLFreeStmt(hstmt, SQL_CLOSE);

    /* verify the prepared row exists */
    rc = exec(hstmt,
        "SELECT COUNT(*) FROM SDB2P16_TEST WHERE ID=6 AND NAME='Frank'");
    CHECK(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);
    SQLINTEGER cnt = 0; SQLLEN cnt_ind = 0;
    CHECK_EQ(SQLBindCol(hstmt, 1, SQL_C_LONG, &cnt, sizeof cnt, &cnt_ind), SQL_SUCCESS);
    CHECK_EQ(SQLFetch(hstmt), SQL_SUCCESS);
    CHECK_EQ((int)cnt, 1);
    SQLFreeStmt(hstmt, SQL_UNBIND);
    SQLFreeStmt(hstmt, SQL_CLOSE);

    /* I7: Transaction ROLLBACK */
    printf("  I7: ROLLBACK — row disappears\n");
    CHECK_EQ(SQLSetConnectAttr(hdbc, SQL_ATTR_AUTOCOMMIT,
                                (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0), SQL_SUCCESS);
    rc = exec(hstmt,
        "INSERT INTO SDB2P16_TEST(ID, NAME) VALUES(99, 'RollbackMe')");
    CHECK(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);
    SQLFreeStmt(hstmt, SQL_CLOSE);
    CHECK_EQ(SQLEndTran(SQL_HANDLE_DBC, hdbc, SQL_ROLLBACK), SQL_SUCCESS);

    rc = exec(hstmt,
        "SELECT COUNT(*) FROM SDB2P16_TEST WHERE ID=99");
    CHECK(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);
    cnt = 0; cnt_ind = 0;
    CHECK_EQ(SQLBindCol(hstmt, 1, SQL_C_LONG, &cnt, sizeof cnt, &cnt_ind), SQL_SUCCESS);
    CHECK_EQ(SQLFetch(hstmt), SQL_SUCCESS);
    CHECK_EQ((int)cnt, 0);  /* rolled back */
    SQLFreeStmt(hstmt, SQL_UNBIND);
    SQLFreeStmt(hstmt, SQL_CLOSE);

    /* I8: Transaction COMMIT */
    printf("  I8: COMMIT — row persists\n");
    rc = exec(hstmt,
        "INSERT INTO SDB2P16_TEST(ID, NAME) VALUES(100, 'CommitMe')");
    CHECK(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);
    SQLFreeStmt(hstmt, SQL_CLOSE);
    CHECK_EQ(SQLEndTran(SQL_HANDLE_DBC, hdbc, SQL_COMMIT), SQL_SUCCESS);

    rc = exec(hstmt,
        "SELECT COUNT(*) FROM SDB2P16_TEST WHERE ID=100");
    CHECK(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);
    cnt = 0; cnt_ind = 0;
    CHECK_EQ(SQLBindCol(hstmt, 1, SQL_C_LONG, &cnt, sizeof cnt, &cnt_ind), SQL_SUCCESS);
    CHECK_EQ(SQLFetch(hstmt), SQL_SUCCESS);
    CHECK_EQ((int)cnt, 1);  /* committed */
    SQLFreeStmt(hstmt, SQL_UNBIND);
    SQLFreeStmt(hstmt, SQL_CLOSE);

    /* restore autocommit */
    CHECK_EQ(SQLSetConnectAttr(hdbc, SQL_ATTR_AUTOCOMMIT,
                                (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0), SQL_SUCCESS);

    /* I9: NULL column handling */
    printf("  I9: NULL column — insert NULL, fetch NULL indicator\n");
    rc = exec(hstmt,
        "INSERT INTO SDB2P16_TEST(ID, NAME, SCORE) VALUES(200, NULL, NULL)");
    CHECK(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);
    SQLFreeStmt(hstmt, SQL_CLOSE);

    rc = exec(hstmt,
        "SELECT NAME, SCORE FROM SDB2P16_TEST WHERE ID=200");
    CHECK(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);
    char null_name[64] = "NOTSET"; SQLLEN null_name_ind = 0;
    SQLDOUBLE null_score = -1.0; SQLLEN null_score_ind = 0;
    CHECK_EQ(SQLBindCol(hstmt, 1, SQL_C_CHAR,   null_name,  sizeof null_name,  &null_name_ind),  SQL_SUCCESS);
    CHECK_EQ(SQLBindCol(hstmt, 2, SQL_C_DOUBLE, &null_score, sizeof null_score, &null_score_ind), SQL_SUCCESS);
    CHECK_EQ(SQLFetch(hstmt), SQL_SUCCESS);
    CHECK_EQ(null_name_ind,  SQL_NULL_DATA);
    CHECK_EQ(null_score_ind, SQL_NULL_DATA);
    printf("    NULL name_ind=%d score_ind=%d — correct\n",
           (int)null_name_ind, (int)null_score_ind);
    SQLFreeStmt(hstmt, SQL_UNBIND);
    SQLFreeStmt(hstmt, SQL_CLOSE);

    /* I10: Large result set — insert 50 rows, fetch all */
    printf("  I10: Large result set (50 rows)\n");
    for (int i = 0; i < 50; i++) {
        char sql[256];
        snprintf(sql, sizeof sql,
                 "INSERT INTO SDB2P16_TEST(ID, NAME, SCORE) "
                 "VALUES(%d, 'Row%d', %.1f)",
                 1000 + i, i, (double)i * 0.5);
        SQLRETURN ir = exec(hstmt, sql);
        CHECK(ir == SQL_SUCCESS || ir == SQL_SUCCESS_WITH_INFO);
        SQLFreeStmt(hstmt, SQL_CLOSE);
    }
    rc = exec(hstmt,
        "SELECT ID FROM SDB2P16_TEST WHERE ID >= 1000 ORDER BY ID");
    CHECK(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);
    SQLINTEGER big_id = 0; SQLLEN big_id_ind = 0;
    CHECK_EQ(SQLBindCol(hstmt, 1, SQL_C_LONG, &big_id, sizeof big_id, &big_id_ind), SQL_SUCCESS);
    int big_rows = 0;
    while ((rc = SQLFetch(hstmt)) == SQL_SUCCESS ||
            rc == SQL_SUCCESS_WITH_INFO)
        big_rows++;
    CHECK_EQ(rc, SQL_NO_DATA);
    CHECK_EQ(big_rows, 50);
    printf("    Fetched %d rows — OK\n", big_rows);
    SQLFreeStmt(hstmt, SQL_UNBIND);
    SQLFreeStmt(hstmt, SQL_CLOSE);

    /* I11: SQLGetData on unbound VARCHAR column */
    printf("  I11: SQLGetData on unbound VARCHAR column\n");
    rc = exec(hstmt,
        "SELECT ID, NAME FROM SDB2P16_TEST WHERE ID=1 FETCH FIRST 1 ROW ONLY");
    CHECK(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);
    /* bind column 1, leave column 2 for SQLGetData */
    SQLINTEGER gd_id = 0; SQLLEN gd_id_ind = 0;
    CHECK_EQ(SQLBindCol(hstmt, 1, SQL_C_LONG, &gd_id, sizeof gd_id, &gd_id_ind), SQL_SUCCESS);
    CHECK_EQ(SQLFetch(hstmt), SQL_SUCCESS);
    CHECK_EQ((int)gd_id, 1);
    char gd_name[128] = {0}; SQLLEN gd_name_ind = 0;
    SQLRETURN grc = SQLGetData(hstmt, 2, SQL_C_CHAR,
                                gd_name, sizeof gd_name, &gd_name_ind);
    CHECK(grc == SQL_SUCCESS || grc == SQL_SUCCESS_WITH_INFO);
    CHECK(gd_name_ind > 0);
    printf("    SQLGetData name='%s' ind=%d\n", gd_name, (int)gd_name_ind);
    SQLFreeStmt(hstmt, SQL_UNBIND);
    SQLFreeStmt(hstmt, SQL_CLOSE);

    /* I12: bad SQL → SQL_ERROR + SQLCODE < 0 */
    printf("  I12: bad SQL → SQL_ERROR + negative SQLCODE\n");
    rc = SQLExecDirect(hstmt,
        (SQLCHAR *)"SELECT * FROM SDB2P16_DOES_NOT_EXIST", SQL_NTS);
    CHECK_EQ(rc, SQL_ERROR);
    {
        SQLCHAR state[6] = {0}, msg[512] = {0};
        SQLINTEGER native = 0;
        SQLSMALLINT mlen  = 0;
        SQLRETURN drc = SQLGetDiagRec(SQL_HANDLE_STMT, hstmt, 1,
                                       state, &native, msg, sizeof msg, &mlen);
        CHECK(drc == SQL_SUCCESS || drc == SQL_SUCCESS_WITH_INFO);
        CHECK(native < 0);
        printf("    expected SQLCODE %d state=%.5s\n", (int)native, state);
    }
    SQLFreeStmt(hstmt, SQL_CLOSE);

    /* I13: SQLRowCount after INSERT / UPDATE / DELETE */
    printf("  I13: SQLRowCount for DML\n");
    rc = exec(hstmt,
        "UPDATE SDB2P16_TEST SET SCORE=10.0 WHERE ID <= 3");
    CHECK(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);
    nrows = 0;
    CHECK_EQ(SQLRowCount(hstmt, &nrows), SQL_SUCCESS);
    CHECK_EQ((int)nrows, 3);
    printf("    UPDATE affected %d rows (expected 3)\n", (int)nrows);
    SQLFreeStmt(hstmt, SQL_CLOSE);

    rc = exec(hstmt, "DELETE FROM SDB2P16_TEST WHERE ID >= 1000");
    CHECK(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);
    nrows = 0;
    CHECK_EQ(SQLRowCount(hstmt, &nrows), SQL_SUCCESS);
    CHECK_EQ((int)nrows, 50);
    printf("    DELETE affected %d rows (expected 50)\n", (int)nrows);
    SQLFreeStmt(hstmt, SQL_CLOSE);

    /* I14: SQLPrepare + SQLExecute SELECT with WHERE parameter */
    printf("  I14: SQLPrepare + SQLExecute SELECT with WHERE\n");
    /* Unbind any leftover params from I6 */
    SQLFreeStmt(hstmt, SQL_RESET_PARAMS);

    rc = SQLPrepare(hstmt,
        (SQLCHAR *)"SELECT ID, NAME FROM SDB2P16_TEST WHERE SCORE > ? ORDER BY ID",
        SQL_NTS);
    CHECK(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);

    SQLDOUBLE where_score = 9.0;
    SQLLEN where_score_ind = 0;
    CHECK_EQ(SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT, SQL_C_DOUBLE,
                               SQL_DOUBLE, 15, 0,
                               &where_score, sizeof where_score,
                               &where_score_ind), SQL_SUCCESS);
    rc = SQLExecute(hstmt);
    CHECK(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);
    SQLSMALLINT ncols = 0;
    CHECK_EQ(SQLNumResultCols(hstmt, &ncols), SQL_SUCCESS);
    CHECK_EQ(ncols, 2);
    int where_rows = 0;
    while ((rc = SQLFetch(hstmt)) == SQL_SUCCESS ||
            rc == SQL_SUCCESS_WITH_INFO)
        where_rows++;
    CHECK_EQ(rc, SQL_NO_DATA);
    printf("    SELECT WHERE SCORE > 9.0 returned %d rows\n", where_rows);
    CHECK(where_rows > 0);  /* at least Alice(9.5→10.0) and Diana(9.9→10.0) */
    SQLFreeStmt(hstmt, SQL_RESET_PARAMS);
    SQLFreeStmt(hstmt, SQL_CLOSE);

    /* I15: DROP TABLE */
    printf("  I15: DROP TABLE (cleanup)\n");
    rc = exec(hstmt, "DROP TABLE SDB2P16_TEST");
    CHECK(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);
    SQLFreeStmt(hstmt, SQL_CLOSE);

    /* Verify table is gone */
    rc = SQLExecDirect(hstmt,
        (SQLCHAR *)"SELECT COUNT(*) FROM SDB2P16_TEST", SQL_NTS);
    CHECK_EQ(rc, SQL_ERROR);
    SQLFreeStmt(hstmt, SQL_CLOSE);

cleanup:
    printf("  I16: SQLDisconnect\n");
    if (hstmt != SQL_NULL_HANDLE)
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    if (hdbc != SQL_NULL_HANDLE) {
        SQLDisconnect(hdbc);
        SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
    }
    if (henv != SQL_NULL_HANDLE)
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    printf("=== Phase 16 — Live DB2 integration tests ===\n");

    /* --connect-only flag for db2-connect-test.sh quick check */
    int connect_only = (argc > 1 && strcmp(argv[1], "--connect-only") == 0);

    const char *host   = getenv("DB2_TEST_HOST");
    if (!host) {
        printf("\nSKIPPED: set DB2_TEST_HOST to run integration tests.\n");
        printf("  export DB2_TEST_HOST=127.0.0.1\n");
        printf("  export DB2_TEST_PORT=50000\n");
        printf("  export DB2_TEST_DB=TESTDB\n");
        printf("  export DB2_TEST_USER=db2inst1\n");
        printf("  export DB2_TEST_PASS=db2inst1\n");
        printf("\nOr use: local-integration/tests/run-integration.sh\n");
        return 0;
    }

    const char *port_s = getenv("DB2_TEST_PORT");
    const char *db     = getenv("DB2_TEST_DB");
    const char *uid    = getenv("DB2_TEST_USER");
    const char *pwd    = getenv("DB2_TEST_PASS");
    int port = port_s ? atoi(port_s) : 50000;
    if (!db)  db  = "TESTDB";
    if (!uid) uid = "db2inst1";
    if (!pwd) pwd = "db2inst1";

    if (connect_only) {
        /* Quick connection check only */
        SQLHENV henv = SQL_NULL_HANDLE;
        SQLHDBC hdbc = SQL_NULL_HANDLE;
        SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
        SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
        SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
        char cs[512];
        snprintf(cs, sizeof cs,
                 "HOSTNAME=%s;PORT=%d;DATABASE=%s;UID=%s;PWD=%s",
                 host, port, db, uid, pwd);
        SQLRETURN rc = SQLDriverConnect(hdbc, NULL,
                                        (SQLCHAR *)cs, SQL_NTS,
                                        NULL, 0, NULL, SQL_DRIVER_NOPROMPT);
        int ok = (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);
        if (ok) printf("  ✅ libstaticdb2 connect: OK\n");
        else    { print_diag(SQL_HANDLE_DBC, hdbc, "connect"); printf("  ❌ connect failed\n"); }
        SQLDisconnect(hdbc);
        SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
        return ok ? 0 : 1;
    }

    run_live_tests(host, port, db, uid, pwd);

    printf("\n=== Result: %d passed, %d failed ===\n", passed, failed);
    print_results("p16_integration");
    return 0;
}
