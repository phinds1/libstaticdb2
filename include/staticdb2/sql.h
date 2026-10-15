#ifndef STATICDB2_SQL_H
#define STATICDB2_SQL_H

/*
 * sql.h — ODBC 3.x subset public API for libstaticdb2.
 *
 * Self-contained: does NOT depend on a system sql.h or sqlext.h.
 * Follows ODBC 3.x naming conventions for the subset of handle types,
 * return codes, C/SQL type identifiers, and entry points implemented
 * by this driver.
 */

#include <stdint.h>
#include <stddef.h>

/* ---- Basic ODBC scalar types ---- */
typedef void *          SQLHANDLE;
typedef void *          SQLHENV;
typedef void *          SQLHDBC;
typedef void *          SQLHSTMT;
typedef void *          SQLPOINTER;
typedef char            SQLCHAR;
typedef signed short    SQLSMALLINT;
typedef unsigned short  SQLUSMALLINT;
typedef signed int      SQLINTEGER;
typedef unsigned int    SQLUINTEGER;
typedef int64_t         SQLBIGINT;
typedef uint64_t        SQLUBIGINT;
typedef float           SQLREAL;
typedef double          SQLDOUBLE;
typedef ptrdiff_t       SQLLEN;
typedef size_t          SQLULEN;
typedef short           SQLRETURN;

/* ---- Return codes ---- */
#define SQL_SUCCESS             0
#define SQL_SUCCESS_WITH_INFO   1
#define SQL_STILL_EXECUTING     2
#define SQL_NEED_DATA          99
#define SQL_NO_DATA           100
#define SQL_ERROR              (-1)
#define SQL_INVALID_HANDLE     (-2)

/* ---- Handle types (for SQLAllocHandle / SQLFreeHandle) ---- */
#define SQL_HANDLE_ENV          1
#define SQL_HANDLE_DBC          2
#define SQL_HANDLE_STMT         3
#define SQL_HANDLE_DESC         4

#define SQL_NULL_HANDLE         ((SQLHANDLE)0)

/* ---- NULL / length special values ---- */
#define SQL_NTS                (-3)     /* null-terminated string */
#define SQL_NULL_DATA          (-1)     /* null column/parameter value */
#define SQL_NO_TOTAL           (-4)     /* total length unknown */
#define SQL_DATA_AT_EXEC       (-2)     /* data-at-execute marker */
#define SQL_DEFAULT_PARAM      (-5)     /* use default parameter value */

/* ---- C data types (TargetType in SQLBindCol / SQLGetData) ---- */
#define SQL_C_DEFAULT           0
#define SQL_C_CHAR              1
#define SQL_C_NUMERIC           2
#define SQL_C_SHORT             5
#define SQL_C_LONG              4
#define SQL_C_FLOAT             7
#define SQL_C_DOUBLE            8
#define SQL_C_DATE              9
#define SQL_C_TIME             10
#define SQL_C_TIMESTAMP        11
#define SQL_C_BINARY           (-2)
#define SQL_C_SBIGINT          (-5)
#define SQL_C_BIT              (-7)

/* ---- SQL data types (DataType in SQLDescribeCol) ---- */
#define SQL_UNKNOWN_TYPE        0
#define SQL_CHAR                1
#define SQL_NUMERIC             2
#define SQL_DECIMAL             3
#define SQL_INTEGER             4
#define SQL_SMALLINT            5
#define SQL_FLOAT               6
#define SQL_REAL                7
#define SQL_DOUBLE              8
#define SQL_DATE                9
#define SQL_TIME               10
#define SQL_TIMESTAMP          11
#define SQL_VARCHAR            12
#define SQL_BIGINT            (-5)
#define SQL_TINYINT           (-6)
#define SQL_BIT               (-7)
#define SQL_BINARY            (-2)
#define SQL_VARBINARY         (-3)
#define SQL_LONGVARCHAR       (-1)
#define SQL_LONGVARBINARY     (-4)
/* DB2-specific extensions */
#define SQL_BOOLEAN           (-360)  /* DB2 BOOLEAN — encode/decode as SQL_C_BIT */
#define SQL_DECFLOAT          (-369)  /* DB2 DECFLOAT — opaque binary on wire */

/* ---- Environment attributes (SQLSetEnvAttr / SQLGetEnvAttr) ---- */
#define SQL_ATTR_ODBC_VERSION   200
#define SQL_OV_ODBC2              2
#define SQL_OV_ODBC3              3

/* ---- Connection attributes (SQLSetConnectAttr / SQLGetConnectAttr) ---- */
#define SQL_ATTR_AUTOCOMMIT     102
#define SQL_AUTOCOMMIT_OFF        0
#define SQL_AUTOCOMMIT_ON         1

/* ---- SQLFreeStmt options ---- */
#define SQL_CLOSE               0
#define SQL_DROP                1
#define SQL_UNBIND              2
#define SQL_RESET_PARAMS        3

/* ---- Transaction completion types for SQLEndTran ---- */
#define SQL_COMMIT              0
#define SQL_ROLLBACK            1

/* ---- Nullable indicator (SQLDescribeCol) ---- */
#define SQL_NO_NULLS            0
#define SQL_NULLABLE            1
#define SQL_NULLABLE_UNKNOWN    2

/* ---- Diagnostic fields (SQLGetDiagField) ---- */
#define SQL_DIAG_RETURNCODE     1
#define SQL_DIAG_NUMBER         2
#define SQL_DIAG_ROW_COUNT      3
#define SQL_DIAG_SQLSTATE       4
#define SQL_DIAG_NATIVE         5
#define SQL_DIAG_MESSAGE_TEXT   6

/* ---- Parameter direction flags (SQLBindParameter InputOutputType) ---- */
#define SQL_PARAM_INPUT         1
#define SQL_PARAM_INPUT_OUTPUT  2
#define SQL_PARAM_OUTPUT        4

/* ---- DriverCompletion flags for SQLDriverConnect ---- */
#define SQL_DRIVER_NOPROMPT     0
#define SQL_DRIVER_COMPLETE     1
#define SQL_DRIVER_PROMPT       2
#define SQL_DRIVER_COMPLETE_REQUIRED 3

/* ================================================================== */
/* ODBC subset entry points                                            */
/* ================================================================== */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Handle management ---- */

SQLRETURN SQLAllocHandle(SQLSMALLINT HandleType, SQLHANDLE InputHandle,
                         SQLHANDLE *OutputHandlePtr);

SQLRETURN SQLFreeHandle(SQLSMALLINT HandleType, SQLHANDLE Handle);

/* ---- Environment attributes ---- */

SQLRETURN SQLSetEnvAttr(SQLHENV EnvironmentHandle, SQLINTEGER Attribute,
                        SQLPOINTER ValuePtr, SQLINTEGER StringLength);

SQLRETURN SQLGetEnvAttr(SQLHENV EnvironmentHandle, SQLINTEGER Attribute,
                        SQLPOINTER ValuePtr, SQLINTEGER BufferLength,
                        SQLINTEGER *StringLengthPtr);

/* ---- Connection attributes ---- */

SQLRETURN SQLSetConnectAttr(SQLHDBC ConnectionHandle, SQLINTEGER Attribute,
                            SQLPOINTER ValuePtr, SQLINTEGER StringLength);

SQLRETURN SQLGetConnectAttr(SQLHDBC ConnectionHandle, SQLINTEGER Attribute,
                            SQLPOINTER ValuePtr, SQLINTEGER BufferLength,
                            SQLINTEGER *StringLengthPtr);

/*
 * SQLConnect — connect to a database.
 * ServerName format: "HOST:PORT/DATABASE"  (port defaults to 446 if omitted;
 *                    database required).
 * Pass SQL_NTS (-3) for any NameLength to use strlen.
 */
SQLRETURN SQLConnect(SQLHDBC ConnectionHandle,
                     SQLCHAR *ServerName,          SQLSMALLINT NameLength1,
                     const SQLCHAR *UserName,       SQLSMALLINT NameLength2,
                     const SQLCHAR *Authentication, SQLSMALLINT NameLength3);

/*
 * SQLDriverConnect — connect using a key=value connection string.
 * Recognised keys (case-insensitive):
 *   HOSTNAME or SERVER  — DB2 host name
 *   PORT                — TCP port (default 446)
 *   DATABASE or DB      — database name (RDBNAM)
 *   UID or USERNAME     — user name
 *   PWD or PASSWORD     — password
 * Example: "HOSTNAME=db2host;PORT=50000;DATABASE=SAMPLE;UID=db2inst1;PWD=secret"
 * OutConnectionString and StringLength2Ptr may be NULL.
 * DriverCompletion is accepted but ignored (always SQL_DRIVER_NOPROMPT behaviour).
 */
SQLRETURN SQLDriverConnect(SQLHDBC ConnectionHandle, SQLPOINTER WindowHandle,
                           SQLCHAR *InConnectionString, SQLSMALLINT StringLength1,
                           SQLCHAR *OutConnectionString, SQLSMALLINT BufferLength,
                           SQLSMALLINT *StringLength2Ptr,
                           SQLUSMALLINT DriverCompletion);

SQLRETURN SQLDisconnect(SQLHDBC ConnectionHandle);

/* ---- Statement execution ---- */

/*
 * SQLExecDirect — prepare and execute in one call.
 * Statements beginning with SELECT/WITH/VALUES open a cursor (use SQLFetch).
 * Other statements are executed immediately (use SQLRowCount for row counts).
 * Pass SQL_NTS for TextLength to use strlen.
 */
SQLRETURN SQLExecDirect(SQLHSTMT StatementHandle,
                        SQLCHAR *StatementText, SQLINTEGER TextLength);

/*
 * SQLPrepare — prepare a statement for execution with SQLExecute.
 * Phase 11 limitation: SQLDescribeCol / SQLNumResultCols are only valid after
 * SQLExecute (no pre-execute describe).
 */
SQLRETURN SQLPrepare(SQLHSTMT StatementHandle,
                     SQLCHAR *StatementText, SQLINTEGER TextLength);

SQLRETURN SQLExecute(SQLHSTMT StatementHandle);

/* ---- Result set metadata ---- */

SQLRETURN SQLNumResultCols(SQLHSTMT StatementHandle, SQLSMALLINT *ColumnCountPtr);

/*
 * SQLDescribeCol — return column metadata.
 * ColumnNumber is 1-based.  Valid after SQLExecDirect/SQLExecute for a SELECT.
 * ColumnName may be NULL (suppresses name copy); other pointer args may be NULL.
 */
SQLRETURN SQLDescribeCol(SQLHSTMT StatementHandle, SQLUSMALLINT ColumnNumber,
                         SQLCHAR *ColumnName, SQLSMALLINT BufferLength,
                         SQLSMALLINT *NameLengthPtr, SQLSMALLINT *DataTypePtr,
                         SQLULEN *ColumnSizePtr, SQLSMALLINT *DecimalDigitsPtr,
                         SQLSMALLINT *NullablePtr);

/* ---- Column binding and fetch ---- */

/*
 * SQLBindCol — bind an application buffer to a result-set column (1-based).
 * Binding survives SQLExecute cycles on the same statement handle;
 * cleared by SQLFreeStmt(SQL_UNBIND).
 * Pass TargetValuePtr = NULL to unbind a specific column.
 */
SQLRETURN SQLBindCol(SQLHSTMT StatementHandle, SQLUSMALLINT ColumnNumber,
                     SQLSMALLINT TargetType, SQLPOINTER TargetValuePtr,
                     SQLLEN BufferLength, SQLLEN *StrLen_or_IndPtr);

SQLRETURN SQLFetch(SQLHSTMT StatementHandle);

/*
 * SQLGetData — read one column value after SQLFetch (1-based column number).
 * Works on both bound and unbound columns.
 */
SQLRETURN SQLGetData(SQLHSTMT StatementHandle, SQLUSMALLINT ColumnNumber,
                     SQLSMALLINT TargetType, SQLPOINTER TargetValuePtr,
                     SQLLEN BufferLength, SQLLEN *StrLen_or_IndPtr);

SQLRETURN SQLRowCount(SQLHSTMT StatementHandle, SQLLEN *RowCountPtr);

/* ---- Parameter binding (Phase 12) ---- */

/*
 * SQLBindParameter — bind an application buffer to a SQL parameter marker (?).
 * ParameterNumber is 1-based.
 * Phase 12 supports SQL_PARAM_INPUT only; InputOutputType is accepted but ignored.
 * Supported SQL types: SQL_SMALLINT, SQL_INTEGER, SQL_BIGINT, SQL_REAL,
 *   SQL_FLOAT, SQL_DOUBLE, SQL_CHAR, SQL_VARCHAR, SQL_LONGVARCHAR,
 *   SQL_DECIMAL, SQL_NUMERIC, SQL_DATE, SQL_TIME, SQL_TIMESTAMP,
 *   SQL_BINARY, SQL_VARBINARY.
 * ParameterValuePtr may be NULL only when StrLen_or_IndPtr points to SQL_NULL_DATA.
 */
SQLRETURN SQLBindParameter(SQLHSTMT StatementHandle,
                           SQLUSMALLINT ParameterNumber,
                           SQLSMALLINT  InputOutputType,
                           SQLSMALLINT  ValueType,
                           SQLSMALLINT  ParameterType,
                           SQLULEN      ColumnSize,
                           SQLSMALLINT  DecimalDigits,
                           SQLPOINTER   ParameterValuePtr,
                           SQLLEN       BufferLength,
                           SQLLEN      *StrLen_or_IndPtr);

/*
 * SQLNumParams — return the number of input parameter markers in the prepared
 * statement.  Valid after SQLPrepare; returns 0 if not prepared.
 */
SQLRETURN SQLNumParams(SQLHSTMT StatementHandle,
                       SQLSMALLINT *ParameterCountPtr);

/* ---- Cursor/statement cleanup ---- */
SQLRETURN SQLFreeStmt(SQLHSTMT StatementHandle, SQLUSMALLINT Option);

/* ---- Transaction control (Phase 13 required for full semantics) ---- */
SQLRETURN SQLEndTran(SQLSMALLINT HandleType, SQLHANDLE Handle,
                     SQLSMALLINT CompletionType);

/* ---- Diagnostics ---- */

/*
 * SQLGetDiagRec — retrieve diagnostic record (RecNumber is 1-based).
 * Returns SQL_NO_DATA when RecNumber > number of available records.
 * This driver stores at most one record per handle.
 * SQLState, NativeErrorPtr, MessageText, TextLengthPtr may be NULL.
 */
SQLRETURN SQLGetDiagRec(SQLSMALLINT HandleType, SQLHANDLE Handle,
                        SQLSMALLINT RecNumber,
                        SQLCHAR *SQLState, SQLINTEGER *NativeErrorPtr,
                        SQLCHAR *MessageText, SQLSMALLINT BufferLength,
                        SQLSMALLINT *TextLengthPtr);

SQLRETURN SQLGetDiagField(SQLSMALLINT HandleType, SQLHANDLE Handle,
                          SQLSMALLINT RecNumber, SQLSMALLINT DiagIdentifier,
                          SQLPOINTER DiagInfoPtr, SQLSMALLINT BufferLength,
                          SQLSMALLINT *StringLengthPtr);

#ifdef __cplusplus
}
#endif

#endif /* STATICDB2_SQL_H */
