# lib static db2

This project is a statically compiled DB2 driver for use in musl-built C code.

The goal is to have C code that can be compiled statically inside a C-based batch
job or nginx module.

Since nginx is one of the target deployments it supports non-blocking I/O and
async callbacks.

Because this will be used for executing SQL and not creating database GUIs, the
metadata features of DB2 drivers are not needed in the initial version.

The project allows executing SQL against a DB2 instance and iterating the returned
result set or the single returned value and nothing else.

It is based on the ODBC spec and the wire protocol is an open standard (DRDA).

## Build

```sh
# requires musl-gcc (e.g. apk add musl-dev gcc on Alpine)
make lib          # → libstaticdb2.a
make test         # run all snip fixture tests (no live DB2 required)
```

## Usage — minimal API example

```c
#include <stdio.h>
#include "staticdb2/sql.h"

int main(void) {
    SQLHENV henv = SQL_NULL_HANDLE;
    SQLHDBC hdbc = SQL_NULL_HANDLE;
    SQLHSTMT hstmt = SQL_NULL_HANDLE;

    /* Environment */
    SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
    SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);

    /* Connection */
    SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
    SQLRETURN rc = SQLConnect(hdbc,
        (SQLCHAR *)"db2host", SQL_NTS,  /* server hostname */
        (SQLCHAR *)"db2inst1", SQL_NTS, /* user */
        (SQLCHAR *)"secret",   SQL_NTS  /* password */
    );
    if (rc != SQL_SUCCESS) {
        SQLCHAR msg[256]; SQLCHAR state[6]; SQLINTEGER native;
        SQLSMALLINT len;
        SQLGetDiagRec(SQL_HANDLE_DBC, hdbc, 1, state, &native, msg, sizeof(msg), &len);
        fprintf(stderr, "connect failed: %s\n", msg);
        return 1;
    }

    /* Statement — simple query */
    SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
    SQLExecDirect(hstmt, (SQLCHAR *)
        "SELECT LASTNAME, SALARY FROM EMPLOYEE WHERE EMPNO = '000010'", SQL_NTS);

    SQLCHAR lastname[50];
    SQLDOUBLE salary;
    SQLBindCol(hstmt, 1, SQL_C_CHAR,   lastname, sizeof(lastname), NULL);
    SQLBindCol(hstmt, 2, SQL_C_DOUBLE, &salary,  sizeof(salary),   NULL);

    while (SQLFetch(hstmt) == SQL_SUCCESS)
        printf("%-20s %.2f\n", lastname, salary);

    /* Parameterized INSERT */
    SQLHSTMT hins = SQL_NULL_HANDLE;
    SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hins);
    SQLPrepare(hins, (SQLCHAR *)
        "INSERT INTO LOG(MSG) VALUES(?)", SQL_NTS);

    SQLCHAR msg_val[64] = "hello";
    SQLLEN  msg_ind     = SQL_NTS;
    SQLBindParameter(hins, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
                     sizeof(msg_val), 0, msg_val, sizeof(msg_val), &msg_ind);
    SQLExecute(hins);

    /* Commit */
    SQLSetConnectAttr(hdbc, SQL_ATTR_AUTOCOMMIT,
                      (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0);
    SQLEndTran(SQL_HANDLE_DBC, hdbc, SQL_COMMIT);

    /* Cleanup */
    SQLFreeHandle(SQL_HANDLE_STMT, hins);
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    SQLDisconnect(hdbc);
    SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
    SQLFreeHandle(SQL_HANDLE_ENV, henv);
    return 0;
}
```

Compile as a fully static binary:

```sh
musl-gcc -std=c11 -static -I include example.c libstaticdb2.a -lm -o example
ldd example   # → "not a dynamic executable"
```

## Non-blocking / nginx usage

```c
#include "staticdb2/async.h"   /* sdb2_async_configure */

drda_conn_t *conn = drda_connect(host, port, db, user, pass, errmsg, sizeof(errmsg));
/* Switch to O_NONBLOCK + poll()-retrying callbacks: */
sdb2_async_configure(conn);
/* All drda_* calls now use non-blocking I/O internally. */

/* For nginx: install your own io_send/io_recv callbacks instead:
 *   conn->io_send = my_ngx_send;
 *   conn->io_recv = my_ngx_recv;
 *   conn->io_ctx  = r;  // ngx_http_request_t *
 * Library propagates SDB2_NEED_MORE / SDB2_NEED_WRITE to your handler.
 * See src/async.h for the full nginx usage contract. */
```

## Running against a DB2 container

```sh
# Start IBM DB2 Community Edition
docker run -d --name db2 \
  -e DB2INST1_PASSWORD=secret \
  -e DBNAME=SAMPLE \
  -p 50000:50000 \
  --privileged \
  icr.io/db2_community/db2

# Run live tests
export DB2_TEST_HOST=127.0.0.1
export DB2_TEST_PORT=50000
export DB2_TEST_DB=SAMPLE
export DB2_TEST_USER=db2inst1
export DB2_TEST_PASS=secret
make test
```

## Environment variables

| Variable        | Default     | Description                     |
|-----------------|-------------|---------------------------------|
| DB2_TEST_HOST   | (unset)     | Enable live DB2 tests           |
| DB2_TEST_PORT   | 50000       | DB2 server port                 |
| DB2_TEST_DB     | SAMPLE      | Relational database name        |
| DB2_TEST_USER   | db2inst1    | User for authentication         |
| DB2_TEST_PASS   | db2inst1    | Password (USRIDPWD clear-text)  |
| SDB2_QRYBLKSZ   | (default)   | Override query block size (bytes; e.g. 512 forces CNTQRY) |

## Basing the Spec Off ODBC

The ODBC 3.x specification is an excellent base.

| Layer               | Functions                                              | Complexity      |
|---------------------|--------------------------------------------------------|-----------------|
| Environment handles | SQLAllocHandle, SQLFreeHandle, SQLSetEnvAttr           | Low             |
| Connection handles  | SQLConnect, SQLDriverConnect, SQLDisconnect            | Medium          |
| Statement handles   | SQLAllocHandle, SQLPrepare, SQLExecute, SQLExecDirect  | Medium          |
| Fetching results    | SQLFetch, SQLGetData, SQLBindCol                       | Medium          |
| Parameters          | SQLBindParameter, SQLNumParams                         | Medium          |
| Metadata            | SQLDescribeCol, SQLColAttribute, SQLTables, SQLColumns | High (optional) |
| Transactions        | SQLEndTran, SQLSetConnectAttr                          | Medium          |
| Diagnostics         | SQLGetDiagRec, SQLGetDiagField                         | Low             |

## The Hard Part: The DB2 Wire Protocol (DRDA)

The real challenge is not the ODBC API surface — it's speaking to DB2 on the wire.

- DB2 uses DRDA (Distributed Relational Database Architecture), an IBM open standard (though complex)
- It runs over TCP/IP on port 50000 (default)
- Encoding is EBCDIC or ASCII depending on the host, with DDM (Distributed Data Management) framing
- There are open-source references: the jt400 (Java), python-ibmdb, and go_ibm_db drivers are good reading
- The Apache Derby network server DRDA implementation is also a useful reference

Since this is targeted for static builds it does not support any dynamic library loading.

## References

- DRDA spec: The Open Group DRDA Standard http://www.opengroup.org/dbiop/
- ODBC spec: Microsoft ODBC 3.x API Reference https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/odbc-api-reference
- unixODBC driver development guide: http://www.unixodbc.org/doc/DriversDoc.pdf
- Open source DB2 clients to study: python-ibmdb, go_ibm_db, Apache Derby's DRDA server
