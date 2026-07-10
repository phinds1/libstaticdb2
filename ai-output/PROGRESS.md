# libstaticdb2 — PROGRESS

Last updated: 2026-07-09
Current phase: COMPLETE — all 15 phases done
Overall status: v1 DONE

## Phase status
- [x] P1  Scaffolding + buffer/net
- [x] P2  CCSID conversion
- [x] P3  DSS framing
- [x] P4  DDM objects + codepoints
- [x] P5  Connection handshake
- [x] P6  SQLCA / diagnostics
- [x] P7  FD:OCA descriptors
- [x] P8  Non-query execution (EXCSQLIMM)
- [x] P9  Query execution + handles
- [x] P10 Value decoding
- [x] P11 ODBC public API (core)
- [x] P12 Parameters
- [x] P13 Transactions
- [x] P14 Non-blocking / nginx
- [x] P15 Hardening / packaging

## Files created so far
- include/staticdb2/sdb2.h  — error-code enum, sdb2_ctype_t, opaque handle typedefs, sdb2_value_t tagged union (Phase 10)
- include/staticdb2/sql.h   — ODBC 3.x subset public API: SQL_* constants, ODBC types, SQL* entry-point prototypes (Phase 11)
- src/buf.h / src/buf.c     — growable byte buffer; big-endian u8/u16/u24/u32/u64 put/get; mark/fill trick
- src/net.h / src/net.c     — blocking TCP adapter
- src/ccsid.h / src/ccsid.c — CCSID 1208/367/500/37 conversion; ascii_to_ebcdic/ebcdic_to_ascii tables
- src/dss.h / src/dss.c     — DSS framing: begin_rqsdss/begin_objdss/finalize/read_header
- src/ddm.h / src/ddm.c     — DDM scalar/collection encode/decode; codepoint enum; cp_name debug
- src/drda.h / src/drda.c   — DRDA connection + full handshake + drda_exec_immediate + P9 query funcs
- src/sqlca.h / src/sqlca.c — SQLCAGRP FD:OCA parser; sdb2_sqlca_t struct
- src/fdoca.h / src/fdoca.c — FD:OCA QRYDSC triplet parser; fdoca_col_t (with precision/scale);
                              fdoca_col_wire_size; fdoca_decode_col; sdb2_stmt_get_col (Phase 10)
- src/handles.h / src/handles.c — sdb2_env_t (odbc_version), sdb2_dbc_t (autocommit, diag),
                                   sdb2_stmt_t (bindings, row_count, prepared_sql, is_prepared, is_query);
                                   sdb2_col_binding_t; lifecycle alloc/free (Phase 11)
- src/api.c                 — SQL* ODBC entry points wiring all layers (Phase 11)
- Makefile                  — musl-gcc static build; lib + test targets
- test/p1..p13 .in + .make  — All phase snip tests

## What works / verified
- make lib: zero warnings (-Wall -Wextra, musl-gcc)
- make test: ALL TESTS PASSED for P1..P13
  P12 fixture tests (18 tests, see prior checkpoint detail)
  P13 fixture tests (6 tests):
    T1  SQLSetConnectAttr autocommit stores flag
    T2  autocommit change syncs to conn->autocommit
    T3  SQLEndTran NULL handle → SQL_INVALID_HANDLE
    T4  SQLEndTran disconnected DBC → SQL_ERROR
    T5  SQLEndTran ENV handle → SQL_SUCCESS
    T6  SQLGetConnectAttr autocommit round-trip
  P13 live tests (DB2_TEST_HOST) SKIPPED (no live DB2 in CI)
    P14 fdoca_encode_col DECIMAL(5,2) "123.45" — packed BCD [0x12][0x34][0x5C]
    P15 fdoca_encode_col DECIMAL(5,2) "-0.01" — negative BCD [0x00][0x00][0x1D]
    P16 non-nullable INTEGER — no indicator byte
    P17 SQLNumParams returns 0 for unprepared stmt
    P18 param_bindings survive partial execute-reset
  P12 live tests (DB2_TEST_HOST) SKIPPED (no live DB2 in CI)

## Phase 11 design notes
- sql.h is self-contained (no system sql.h dependency); ODBC types use C99 stdint.h
- drda_connect signature changed: added (char *errmsg, size_t errmsg_len) so
  SQLConnect/SQLDriverConnect can surface connection failure messages to SQLGetDiagRec
- strndup replaced with sdb2_strndup (local helper) for strict C11 without POSIX macros
- sdb2_stmt_t extended: bindings[], n_bindings, row_count, prepared_sql, is_prepared, is_query
- sdb2_dbc_t extended: autocommit, diag_msg, diag_sqlcode
- sdb2_env_t extended: odbc_version
- sdb2_stmt_alloc initialises row_count = -1 (not applicable until DML)
- SQLExecDirect: detects SELECT/WITH/VALUES by keyword scan → uses prepare+open_query;
  other statements use drda_exec_immediate
- SQLExecute: for query: drda_open_query (section already assigned by SQLPrepare);
  for non-query: drda_exec_immediate (re-executes stored SQL text)
- SQLFetch: calls drda_fetch, then writes to all active bindings via copy_value_to_c_buf
- copy_value_to_c_buf: handles SQL_C_CHAR/SHORT/LONG/SBIGINT/FLOAT/DOUBLE/BINARY/BIT
- SQLGetDiagRec: surfaces conn diag_msg (for DBC handles) or stmt diag_msg (for STMT handles);
  RecNumber > 1 → SQL_NO_DATA (at most one record per handle)
- SQLEndTran: Phase 13 placeholder (returns SQL_SUCCESS)

## In-progress detail
- Phase: COMPLETE
- All 15 phases done. make test → ALL TESTS PASSED (P1–P15, zero warnings).
- Static binary: musl-gcc -static, ldd → "not a dynamic executable"

## Phase 15 design notes
- Parser fuzz: DSS header, DDM header, FD:OCA QRYDSC, fdoca_decode_col
  all fed empty / truncated / garbage / all-zeros / all-0xFF inputs.
  All return error codes (no crash / SIGSEGV).
- Handle lifecycle: 16 alloc/free cycles (valgrind target).
- Static binary verified: file output "statically linked".
- README.md updated: build instructions, minimal API example, non-blocking
  usage, Docker container instructions, env var table.
- SDB2_NEED_WRITE = -10 added to sdb2.h (socket not writable yet).

## Phase 14 design notes
- I/O vtable added to drda_conn_t: io_send, io_recv, io_ctx
  Default (blocking): net_blocking_send / net_blocking_recv
  Non-blocking: sdb2_async_polled_send / sdb2_async_polled_recv
- drda_flush + drda_recv_n now call through conn->io_send / conn->io_recv
  (previously called net_send_all / net_recv_some directly)
- net.h/net.c additions:
    net_set_nonblocking(fd)       — sets O_NONBLOCK
    net_blocking_send/recv        — vtable wrappers for blocking path
    net_nb_send/recv              — EAGAIN-aware non-blocking callbacks
    net_nb_send → SDB2_NEED_WRITE on EAGAIN or partial write
    net_nb_recv → SDB2_NEED_MORE  on EAGAIN
- sdb2.h: added SDB2_NEED_WRITE = -10
- src/async.h + src/async.c: nginx usage contract documented in header;
    sdb2_async_configure(conn) — sets O_NONBLOCK + polled vtable
    sdb2_async_polled_send/recv — poll()-retrying non-blocking callbacks
- async.c added to LIB_SRC in Makefile

## Phase 13 design notes
- drda_conn_t.autocommit field added (default 1); set from dbc->autocommit on connect
- drda_exec_immediate + drda_execute_prepared now use conn->autocommit for RDBCMTOK
  (0xF1 = auto-commit allowed; 0xF0 = manual)
- SQLSetConnectAttr(SQL_ATTR_AUTOCOMMIT) syncs dbc->autocommit → conn->autocommit if connected
- SQLEndTran: dispatches to drda_commit (RDBCMM) or drda_rollback (RDBRLLBCK)
  Both parse ENDUOWRM reply; SQLCARD errors propagated via diag_sqlcode
- ENV handle SQLEndTran: trivial success (no connection loop in Phase 13)
- ENDUOWRM parsing: consumes SVRCOD + SQLCARD; ignores UOWDSP (disposition byte)

## Phase 12 design notes
- sdb2_param_binding_t added to handles.h (analogous to sdb2_col_binding_t)
- sdb2_stmt_t extended: param_bindings[], n_param_bindings, param_desc (fdoca_result_t)
- fdoca.c extended with fdoca_sql_type_to_col + fdoca_encode_col (encoding direction)
- fdoca_encode_col: writes nullable indicator (if col->nullable) + value bytes in DRDA wire format
  Supported: SMALLINT, INTEGER, BIGINT, FLOAT4, FLOAT8, CHAR, VARCHAR, LONG VARCHAR,
             FIXBYTE, VARBYTE, LONGVARBYTE, DATE, TIME, TIMESTAMP, DECIMAL (packed BCD)
- Packed BCD encoding: sign nibble 0xC (positive), 0xD (negative); negative zero → 0xC
- ddm.h: added CP_SQLDTA = 0x2412
- drda.c: drda_describe_params → DSCSQLSTT; drda_execute_prepared → EXCSQLSTT + SQLDTA
  drda_describe_params is called lazily inside drda_execute_prepared (first execute only)
- stmt_reset(): now also clears param_bindings + param_desc (full new-prepare reset)
- SQLExecute: if n_param_bindings > 0 → drda_execute_prepared; else → drda_exec_immediate
- SQLFreeStmt(SQL_RESET_PARAMS): frees param_bindings and param_desc
- sql.h: added SQL_PARAM_INPUT/INPUT_OUTPUT/OUTPUT constants; SQLBindParameter + SQLNumParams

## Environment notes
- DB2 test host: set DB2_TEST_HOST/PORT/DB/USER/PASS for live tests
- Set SDB2_QRYBLKSZ=512 to force small blocks (exercises CNTQRY on live test)
- EXCSQLIMM package: NULLID.SYSSHA00 (all zeros contoken) — may need binding on some DB2 LUW
- Negotiated MGRLVLs from server: TBD (Phase 5 live test needed)
- References: ./references/{derby,jtopen,python-ibmdb,go_ibm_db}
