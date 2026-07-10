# libstaticdb2 — Phased Implementation Plan

A phased delivery plan for **libstaticdb2**: a statically compiled, musl-friendly
DB2 driver written in C. It exposes an ODBC 3.x *subset* API and speaks the DB2
wire protocol (DRDA/DDM) directly over TCP — no dynamic library loading, no
metadata catalog features, no IBM CLI driver dependency.

This document is the single source of truth for the build. It is written to be
executed by **Claude Sonnet 4.6** one phase at a time. Each phase is scoped to
fit comfortably inside a single Sonnet 4.6 context window.

---

## 0. How to use this plan (READ FIRST, EVERY SESSION)

**Execution model**

1. Open `./ai-output/PROGRESS.md`. If it does not exist, you are starting Phase 1.
   Create it from the template in Appendix A.
2. Find the first phase whose status is not `DONE`. That is your **current phase**.
3. Read *only* the reference files listed in that phase (plus files you have
   already created). Do **not** read the whole `references/` tree — it will
   overflow the context window.
4. Implement the phase. Keep going until the phase's **Acceptance** checklist
   passes.
5. **Checkpoint**: update `./ai-output/PROGRESS.md` (see Appendix A). Commit if a
   VCS is in use. Then **stop and tell the user the context can be cleared** and
   the next session should resume at the next phase.
6. If the context window fills *mid-phase*, checkpoint immediately: record exactly
   which tasks in the phase are done, which files exist, and the next concrete
   step. A fresh session must be able to resume from `PROGRESS.md` alone.

**Golden rules**

- One phase per session where possible. Never start a new phase without
  checkpointing the previous one.
- Prefer small, compiling increments. The tree must compile at every checkpoint
  (even if functionality is stubbed).
- Study references, do not copy them. Derby/JTOpen are Java/GPL-adjacent
  (Apache-2.0 for Derby, EPL for JTOpen); use them to understand the *protocol*,
  and write original C. Do not paste license-encumbered code.
- ASCII-first. Target modern DB2 LUW which supports UTF-8/ASCII agents; treat
  EBCDIC as a later hardening task, not a Phase-1 blocker.

---

## 1. Reference material (downloaded to `./references/`)

| Dir                     | What it is                                    | Use it for                                                                 |
|-------------------------|-----------------------------------------------|----------------------------------------------------------------------------|
| `references/derby`      | Apache Derby DRDA **client** + **server** (Java, Apache-2.0) | PRIMARY. Pure open DRDA/DDM implementation. Wire protocol truth.           |
| `references/jtopen`     | IBM Toolbox for Java / JTOpen (Db2 for i)     | Secondary DRDA reference; datastream + CCSID/encoding details.             |
| `references/python-ibmdb` | Python driver (wraps IBM CLI driver)        | ODBC/CLI **API shape** and usage patterns (not wire protocol).            |
| `references/go_ibm_db`  | Go driver (wraps IBM CLI driver)              | ODBC/CLI API shape, handle lifecycle, fetch loops.                         |

**Key Derby client files** (under
`references/derby/java/org.apache.derby.client/org/apache/derby/client/net/`):

- `CodePoint.java` — every DRDA codepoint (EXCSAT=0x1041, ACCSEC=0x106D,
  SECCHK=0x106E, ACCRDB=0x2001, PRPSQLSTT=0x200D, OPNQRY=0x200C, EXCSQLSTT=0x200B,
  EXCSQLIMM=0x200A, CNTQRY=0x2006, QRYDTA=0x241B, SQLCARD=0x2408, reply RMs
  ACCRDBRM=0x2201, OPNQRYRM=0x2205, ENDQRYRM=0x220B, …). The single most useful file.
- `DssConstants.java` — DSS envelope constants (RQSDSS=0x01, OBJDSS=0x03,
  chaining bit GDSCHAIN=0x40).
- `Request.java` / `Reply.java` — low-level DSS/DDM buffer read+write, length
  markers, big-endian scalars. Model the C buffer layer on these.
- `NetConnectionRequest.java` / `NetConnectionReply.java` — the
  EXCSAT→ACCSEC→SECCHK→ACCRDB handshake, byte for byte.
- `NetStatementRequest.java` / `NetStatementReply.java` — PRPSQLSTT/OPNQRY/
  EXCSQLSTT/EXCSQLIMM/CNTQRY/DSCSQLSTT command building.
- `FdocaConstants.java`, `FdocaSimpleDataArray.java`, `Typdef.java`,
  `NetCursor.java` — FD:OCA descriptors and row/column decoding.
- `NetSqlca.java` — SQLCA (error/warning) parsing from SQLCARD.
- `CcsidManager.java`, `EbcdicCcsidManager.java`, `Utf8CcsidManager.java` —
  character-set conversion.
- `DRDAConstants.java` (under
  `.../org.apache.derby.commons/org/apache/derby/shared/common/reference/`) —
  protocol-wide constants, product IDs, type numbers.

**Server-side** (`.../org.apache.derby.server/org/apache/derby/impl/drda/`) —
useful when you need to see how a server *parses* what the client sends
(`DRDAConnThread.java`, `FdocaConstants`, `DssTrace`).

---

## 2. Target architecture

```
+-------------------------------------------------------------+
|  ODBC subset API   (include/staticdb2/sql.h  — SQL* funcs)  |  Phases 9-13
+-------------------------------------------------------------+
|  DRDA command layer (connect, prepare, execute, fetch, txn) |  Phases 5-8
+-------------------------------------------------------------+
|  DDM object layer   (codepoints, scalar/collection encode)  |  Phase 4
+-------------------------------------------------------------+
|  DSS framing layer  (envelope, chaining, correlation ids)   |  Phase 3
+-------------------------------------------------------------+
|  CCSID conversion   (ASCII/UTF-8 first, EBCDIC later)       |  Phase 2b
+-------------------------------------------------------------+
|  Byte buffer + net I/O (big-endian, blocking core)          |  Phases 1-2
+-------------------------------------------------------------+
```

Design constraint for **nginx**: the core is written as a **non-blocking state
machine over a byte buffer**. Blocking socket I/O is a thin adapter used by the
CLI/batch path; nginx supplies its own event loop and feeds bytes in/out. Never
call blocking `recv()` from inside protocol-parsing code — parse from a buffer,
return "need more bytes" when short. This is decided in Phase 1 and honored
throughout.

**Proposed source layout** (created in Phase 1):

```
include/staticdb2/
    sql.h            # ODBC subset public API + SQL_* constants
    sdb2.h           # internal-but-shared types
src/
    buf.c   buf.h    # byte buffer, big-endian scalars   (P1)
    net.c   net.h    # blocking socket adapter           (P1)
    ccsid.c ccsid.h  # charset conversion                (P2)
    dss.c   dss.h    # DSS framing                        (P3)
    ddm.c   ddm.h    # codepoints + object encode/decode  (P4)
    drda.c  drda.h   # command flows: connect/exec/fetch  (P5-P8)
    fdoca.c fdoca.h  # descriptors + value decode         (P7,P10)
    sqlca.c sqlca.h  # diagnostics                        (P6)
    api.c            # ODBC SQL* entry points             (P9-P13)
    handles.c        # env/dbc/stmt handle structs        (P9)
test/
    *.in  *.make     # snip-based tests (see snip/ scripts)
Makefile             # musl static build
```

Build baseline (Phase 1): `musl-gcc -static -std=c11 -Wall -Wextra` producing
`libstaticdb2.a` and test binaries. No external deps.

---

## 3. Global conventions (apply in every phase)

- **Errors**: every internal function returns `int` (`0` = ok, negative = error
  code from `sdb2.h`). Diagnostics accumulate in the owning handle for
  `SQLGetDiagRec`. Never `exit()` inside the library.
- **Endianness**: DRDA is big-endian on the wire. All scalars go through
  `buf_put_u16/u32` / `buf_get_u16/u32` helpers — never raw casts.
- **Memory**: caller-owned buffers for API; library-owned arenas per statement,
  freed on `SQLFreeHandle`. No global mutable state (nginx worker safety).
- **No dynamic loading**: everything static. No `dlopen`, no plugin ODBC driver
  manager. `SQLConnect` talks DRDA directly.
- **Testing**: use the repo's `snip/` harness (`snipcat`, `sniprun`). Each phase
  ships a `test/<phase>.in` + `<phase>.make`. Tests must run against either a
  live DB2 (env `DB2_TEST_HOST/PORT/DB/USER/PASS`) or recorded byte fixtures for
  protocol layers (preferred for CI — no DB2 needed).

---

## 4. Phase catalogue

Each phase below is self-contained. Do the **Reference reading** first, then the
**Tasks**, then verify **Acceptance**, then **Checkpoint**.

---

### Phase 1 — Scaffolding, build system, buffer + net primitives
**Goal:** A musl static build that compiles a library archive and a runnable test
binary. Byte-buffer abstraction and a blocking TCP adapter exist and are tested.

**Reference reading:** `Request.java` (buffer growth, `writeScalar*`, length
markers), `Reply.java` (buffer read, `readScalar*`). Skim only — you are copying
the *shape*, not the code.

**Tasks:**
1. Create the source layout from §2 and a `Makefile` (musl-gcc, `-static`,
   targets: `all`, `lib` → `libstaticdb2.a`, `test`, `clean`).
2. `buf.[ch]`: growable byte buffer with position/limit, big-endian
   `put/get u8/u16/u24/u32/u64`, `put_bytes/get_bytes`, `mark/rewind`, and a
   "layout length" trick (reserve 2 bytes, backfill length) mirroring Derby's
   `markLengthBytes`/`updateLengthBytes`.
3. `net.[ch]`: `net_connect(host, port)`, `net_send_all`, `net_recv_some`,
   `net_close`. Blocking. This is the *only* place that blocks.
4. `sdb2.h`: error-code enum, opaque handle typedefs (forward decls).
5. First snip test: round-trip encode/decode of every scalar width through `buf`.

**Acceptance:**
- `make` produces `libstaticdb2.a` with zero warnings under `-Wall -Wextra`.
- `make test` runs and the buffer round-trip test passes.
- `net_connect` to a TCP echo (or `DB2_TEST_HOST` if set) succeeds or fails
  cleanly with a diagnostic (no crash).

**Checkpoint:** update PROGRESS.md → Phase 1 DONE, list created files.

---

### Phase 2 — CCSID conversion (ASCII/UTF-8 first)
**Goal:** Convert between the driver's UTF-8 strings and the wire code page.
ASCII/UTF-8 path complete; EBCDIC stubbed with a clear TODO.

**Reference reading:** `CcsidManager.java` (interface), `Utf8CcsidManager.java`
(the easy path you implement now), `EbcdicCcsidManager.java` (the table you will
port later — skim, note it uses a 256-entry conversion table).

**Tasks:**
1. `ccsid.[ch]`: `ccsid_to_wire(ccsid, utf8, len, out)` and
   `ccsid_from_wire(...)`. Implement CCSID 1208 (UTF-8) and 367 (ASCII)
   fully.
2. Add the EBCDIC (CCSID 500/037) 256-byte tables but gate behind a feature that
   returns "unsupported" for now — do not block the ASCII path.
3. DDM control data (codepoint names, RDB names) is EBCDIC even on ASCII agents:
   port the small EBCDIC table needed for `RDBNAM`/`EXTNAM` field encoding used
   in Phase 5. (Derby: `EbcdicCcsidManager` + `NetConnectionRequest.buildRDBNAM`.)

**Acceptance:** round-trip tests for UTF-8 and ASCII pass; EBCDIC table for
control fields converts a known string (e.g. a DB name) to the expected bytes
(fixture from Derby trace or hand-computed).

**Checkpoint:** PROGRESS.md → Phase 2 DONE.

---

### Phase 3 — DSS framing layer (DDM envelope)
**Goal:** Read and write DSS envelopes: the 6-byte DDM header (length, magic
`0xD0`, format byte, correlation id), chaining, and the notion of RQSDSS /
OBJDSS / RPYDSS.

**Reference reading:** `DssConstants.java`, and in `Request.java` the
`createCommand`/`createObject`/`finalizeDssLength`/chaining logic; in `Reply.java`
the `readDssHeader` logic. `DRDAConstants.DATA_STREAM_STRUCTURE_MAX_LENGTH`.

**Tasks:**
1. `dss.[ch]`: `dss_begin_request(buf, corrId, chained)`,
   `dss_begin_object(buf)`, `dss_finalize(buf)` (backfills 2-byte length, sets
   magic `0xD0`, format nibble, GDS chain bits).
2. `dss_read_header(buf, &hdr)` returning length, format, corrId, chain flag;
   handle DSS **continuation** (length 0x7FFF → more segments).
3. Correlation-id counter on the connection object.

**Acceptance:** encode a request DSS, feed it to your own reader, get identical
header fields. Cross-check byte layout against a Derby packet trace fixture
committed under `test/fixtures/`.

**Checkpoint:** PROGRESS.md → Phase 3 DONE.

---

### Phase 4 — DDM object layer (codepoints + scalar/collection encode)
**Goal:** Encode/decode DDM objects: a codepoint + length-delimited value
(scalars) and nested collections. Port the codepoint constants you need.

**Reference reading:** `CodePoint.java` (transcribe the constants used in
Phases 5–13 into `ddm.h`), `Request.java` `writeScalar*`/`writeLengthCodePoint`,
`Reply.java` `readLengthAndCodePoint`.

**Tasks:**
1. `ddm.h`: enum/#defines for the codepoints listed in §1 (commands, reply
   messages, and instance-variable codepoints: EXTNAM, SRVNAM, SRVRLSLV,
   MGRLVLLS, RDBNAM, RDBACCCL, PRDID, TYPDEFNAM, TYPDEFOVR, SECMEC, SECTKN,
   USRID, PASSWORD, PKGID/PKGNAMCSN, RDBCMTOK, QRYBLKSZ, etc.).
2. `ddm.[ch]`: `ddm_put_scalar(buf, cp, bytes, len)`,
   `ddm_put_string(buf, cp, ccsid, str)`, `ddm_begin_collection(buf, cp)` /
   `ddm_end_collection`, and readers `ddm_read_cp_len` / `ddm_read_scalar`.
3. A codepoint-name table (from `CodePointNameTable.java`) for readable trace/
   error messages.

**Acceptance:** build a small known object (e.g. an EXTNAM scalar) and verify
bytes; parse it back. Unit test nested collection begin/end length backfill.

**Checkpoint:** PROGRESS.md → Phase 4 DONE.

---

### Phase 5 — Connection handshake (EXCSAT → ACCSEC → SECCHK → ACCRDB)
**Goal:** Establish a DRDA connection to a real DB2 and reach "RDB accessed"
state. This is the make-or-break integration phase.

**Reference reading (study closely):** `NetConnectionRequest.java`
(`buildEXCSAT`, `buildACCSEC`, `buildSECCHK`, `buildACCRDB`, `buildRDBNAM`,
`buildPRDID`, `buildPRDDTA`, `buildTYPDEFNAM`/`buildTYPDEFOVR`, `buildMGRLVLLS`),
`NetConnectionReply.java` (`parseEXCSATreply`, `parseACCSECreply`,
`parseSECCHKreply`, `parseACCRDBreply`). `DRDAConstants` for MGRLVL values and
product id. `NetConnection.java` `flowServerAttributesAndKeyExchange` for order.

**Tasks:**
1. `drda.[ch]`: connection object (socket via `net`, corrId counter, negotiated
   manager levels, server product info, target CCSID).
2. `drda_exchange_server_attrs` → send EXCSAT (EXTNAM, SRVNAM, SRVRLSLV,
   MGRLVLLS: AGENT, SQLAM, RDB, SECMGR, CMNTCPIP, level numbers), parse EXCSATRD.
3. `drda_access_security` → send ACCSEC (SECMEC = USRIDPWD = 0x03 for the first
   version; note DB2 may require encrypted/`EUSRIDPWD`; support clear USRIDPWD
   against a test instance configured for it), parse ACCSECRD (server SECMEC
   list).
4. `drda_security_check` → send SECCHK (USRID + PASSWORD), parse SECCHKRM
   (SECCHKCD == 0 means success; map non-zero to a diagnostic).
5. `drda_access_rdb` → send ACCRDB (RDBNAM, RDBACCCL=SQLAM, PRDID, PRDDTA,
   TYPDEFNAM=QTDSQLASC for ASCII agent, TYPDEFOVR CCSIDs), parse ACCRDBRM.
   Handle RDBNFNRM (db not found) / RDBATHRM (not authorized) as clean errors.
6. Chain EXCSAT+ACCSEC in one send, then SECCHK, then ACCRDB, per Derby's flow.

**Acceptance:**
- Against `DB2_TEST_*` env: full handshake succeeds; connection reaches
  RDB-accessed state; `drda_disconnect` closes cleanly.
- Wrong password → SECCHKRM surfaced as a specific diagnostic, no crash.
- If no DB2 is available: verify request bytes against a committed fixture
  captured from Derby, and parse a captured ACCRDBRM fixture.

> This phase is large. If context fills after the request path but before reply
> parsing, checkpoint with exactly that state.

**Checkpoint:** PROGRESS.md → Phase 5 DONE (record negotiated MGRLVL values seen
from the test server, they inform later phases).

---

### Phase 6 — SQLCA / diagnostics (SQLCARD parsing, reply-message dispatch)
**Goal:** Parse the SQLCA carried in SQLCARD replies and the family of DRDA reply
messages (RM), turning them into driver diagnostics.

**Reference reading:** `NetSqlca.java`, `NetConnectionReply`/`NetStatementReply`
reply-message parsing (`parseSQLCARD`, `parse*RM`), `CodePoint` RM constants
(SQLCARD, ENDQRYRM, ENDUOWRM, ABNUOWRM, DTAMCHRM, QRYPOPRM, RDBUPDRM, CMDCHKRM,
SYNTAXRM, PRCCNVRM, SQLERRRM).

**Tasks:**
1. `sqlca.[ch]`: SQLCA struct (sqlcode, sqlstate[5], message tokens, warning
   flags) and `sqlca_parse(buf)` from a SQLCARD body (handle the null indicator:
   FF = no SQLCA, else parse fixed + var portions per FD:OCA SQLCA grp).
2. Generic reply dispatcher `drda_read_reply(conn, expected_cp)`: loop over DSS
   objects, collect SQLCARD into current diagnostic, recognize RM codepoints,
   store `svrcod` severity, stop at expected reply or ENDQRYRM/error RM.
3. Map sqlstate → the driver's diagnostic list consumed later by SQLGetDiagRec.

**Acceptance:** parse a captured SQLCARD (both the "no error" FF case and a real
error, e.g. table-not-found SQLCODE -204) into correct sqlcode/sqlstate. Severity
threshold logic (svrcod >= 8 = error) unit-tested.

**Checkpoint:** PROGRESS.md → Phase 6 DONE.

---

### Phase 7 — FD:OCA descriptors (column/result-set metadata)
**Goal:** Parse FD:OCA descriptors (SQLDARD / QRYDSC) into an internal column
list: for each column its DRDA/SQL type, length, scale, ccsid, nullability.

**Reference reading:** `FdocaConstants.java`, `FdocaSimpleDataArray.java`,
`Typdef.java`, and in `NetStatementReply`/`NetCursor` the descriptor parsing
(`parseSQLDARD`, `parseQRYDSC`, `calculateColumnOffsetsForColumn`).
`DRDAConstants` DRDA type numbers (e.g. `DRDA_TYPE_NINTEGER`, `_NVARCHAR`, …).

**Tasks:**
1. `fdoca.[ch]`: `fdoca_column` struct + `fdoca_result` (array of columns).
2. `fdoca_parse_qrydsc(buf, &result)` — walk the FD:OCA triplets (SDA, group,
   row descriptor), resolving each column's simple-data-array to type/len/scale.
3. Maintain the mapping DRDA type → an internal `SDB2_C_*` C type tag used by
   Phase 10 decoding and Phase 11 `SQLBindCol`/`SQLGetData`.

**Acceptance:** parse a captured QRYDSC for a known `SELECT` into the expected
column set (name where available, type, length, nullable). Table-driven test
covering INTEGER, SMALLINT, BIGINT, DECIMAL, CHAR, VARCHAR, DATE, TIME,
TIMESTAMP, and their nullable variants.

**Checkpoint:** PROGRESS.md → Phase 7 DONE.

---

### Phase 8 — Non-query execution (EXCSQLIMM / EXCSQLSTT no result set)
**Goal:** Execute statements that return no result set (INSERT/UPDATE/DELETE/DDL)
and report the update count.

**Reference reading:** `NetStatementRequest.java` `buildEXCSQLIMM`,
`buildEXCSQLSTT`, section/package handling (`buildPKGNAMCSN`, `Section`),
`NetStatementReply` reply handling; note the implicit package (SQLAM default
package) used for immediate execution.

**Tasks:**
1. Minimal "section/package" concept: DB2 requires a package. Use the DRDA
   default dynamic package (PKGID/PKGNAMCSN) that Derby uses for
   EXCSQLIMM/PRPSQLSTT — port the constants, don't invent.
2. `drda_exec_immediate(conn, sql)` → EXCSQLIMM (chained with the SQL statement
   in an SQLSTT object), read reply, parse SQLCARD, extract ROW-count from
   SQLCA/RDBUPDRM.
3. Wire the result into a statement handle (created in Phase 9; for now expose an
   internal call and test it directly).

**Acceptance:** against test DB2: `CREATE TABLE`, `INSERT`, `DELETE` succeed and
report correct row counts; a bad statement returns the right SQLCODE via Phase 6
diagnostics. Fixture fallback if no DB2.

**Checkpoint:** PROGRESS.md → Phase 8 DONE.

---

### Phase 9 — Query execution + ODBC handle model
**Goal:** Open a cursor and fetch rows at the protocol level, and stand up the
ODBC handle structs (env/dbc/stmt) that the public API will use.

**Reference reading:** `NetStatementRequest.java` `buildPRPSQLSTT`,
`buildOPNQRY`, `buildCNTQRY` (query block size QRYBLKSZ, QRYROWSET);
`NetStatementReply` `parseOPNQRYRM`/`parseENDQRYRM`; `NetCursor.java` (query data
buffering, block continuation). For the API handle shapes: `python-ibmdb` and
`go_ibm_db` handle/stmt lifecycles.

**Tasks:**
1. `handles.c`: `sdb2_env`, `sdb2_dbc` (owns `drda` connection), `sdb2_stmt`
   (owns cursor state, column descriptors, row buffer, diagnostics).
2. `drda_prepare(stmt, sql)` → PRPSQLSTT (+ describe output → SQLDARD via
   Phase 7), storing column descriptors on the stmt.
3. `drda_open_query(stmt)` → OPNQRY, parse OPNQRYRM + QRYDSC (if not from
   prepare) + first QRYDTA block; detect ENDQRYRM (empty/'end).
4. `drda_fetch(stmt)` → return next row from the current block; when the block is
   exhausted and cursor not closed, send CNTQRY to pull the next block; set
   "no more data" on ENDQRYRM.

**Acceptance:** against test DB2: `SELECT` over a multi-row table opens, and
`drda_fetch` walks all rows across at least one CNTQRY block boundary (use a
small QRYBLKSZ to force continuation). ENDQRYRM ends the loop.

**Checkpoint:** PROGRESS.md → Phase 9 DONE.

---

### Phase 10 — Row/column value decoding (FD:OCA → C values)
**Goal:** Decode QRYDTA row bytes into typed C values using Phase 7 descriptors,
including null indicators.

**Reference reading:** `NetCursor.java` value extraction
(`getStringData`/`getIntData`/… offset math), `FdocaSimpleDataArray.java`,
`Typdef.java`. DRDA numeric encodings: packed/zoned decimal, big-endian ints,
`DRDA_TYPE_*` in `DRDAConstants`.

**Tasks:**
1. `fdoca_decode.*` (extend `fdoca.c`): per-column decoders for INTEGER,
   SMALLINT, BIGINT, DECIMAL (packed → string/double), FLOAT/DOUBLE, CHAR,
   VARCHAR/LONG VARCHAR (length-prefixed), DATE/TIME/TIMESTAMP (ISO strings),
   and null-indicator handling (nullable types carry a leading indicator byte).
2. Convert character data via Phase 2 CCSID using the column ccsid.
3. Expose `sdb2_stmt_get_col(stmt, idx, &value)` returning a tagged union.

**Acceptance:** a `SELECT` returning one row of each supported type decodes to
the expected values (compare against known inserted data). NULLs report null.
Packed-decimal correctness unit-tested with fixtures.

**Checkpoint:** PROGRESS.md → Phase 10 DONE.

---

### Phase 11 — ODBC public API surface (core)
**Goal:** Implement the ODBC subset entry points wiring the layers together.
This is what consumers call.

**Reference reading:** ODBC 3.x semantics (README table). API shape/return-code
conventions from `go_ibm_db`/`python-ibmdb`. Signatures per ODBC:
`include/staticdb2/sql.h`.

**Tasks:**
1. `include/staticdb2/sql.h`: SQL_* return codes (`SQL_SUCCESS`,
   `SQL_SUCCESS_WITH_INFO`, `SQL_ERROR`, `SQL_NO_DATA`), handle types, C/SQL type
   defines — a *minimal* subset, self-contained (do NOT depend on a system
   `sql.h`).
2. `api.c`:
   - `SQLAllocHandle` / `SQLFreeHandle` (ENV, DBC, STMT), `SQLSetEnvAttr`.
   - `SQLConnect` / `SQLDriverConnect` (parse `HOSTNAME;PORT;DATABASE;UID;PWD`) /
     `SQLDisconnect` → Phase 5.
   - `SQLExecDirect` (query → Phase 9 open; non-query → Phase 8),
     `SQLPrepare` + `SQLExecute`.
   - `SQLNumResultCols`, `SQLDescribeCol` (from Phase 7 descriptors — allowed as
     it's needed to consume results, not the optional catalog metadata).
   - `SQLBindCol`, `SQLFetch`, `SQLGetData` → Phase 10 decoding with C-type
     conversion into caller buffers; `SQLRowCount`.
   - `SQLFreeStmt` (close cursor / unbind / drop).
   - `SQLGetDiagRec` / `SQLGetDiagField` → Phase 6 diagnostics.

**Acceptance:** an end-to-end C program (in `test/`) using ONLY the public API
connects, runs a `SELECT`, binds columns, fetches all rows, prints them, and
runs an `INSERT` checking `SQLRowCount` — all linked against the static
`libstaticdb2.a`. Error path returns `SQL_ERROR` + a readable diagnostic.

**Checkpoint:** PROGRESS.md → Phase 11 DONE. This is the first user-visible
milestone (read path complete).

---

### Phase 12 — Parameters (input binding)
**Goal:** Support parameter markers so prepared statements can take input values.

**Reference reading:** `NetStatementRequest.java` `buildEXCSQLSTT` with SQLDTA
(input data), `buildSQLDTA`/FD:OCA input descriptor; `NetPreparedStatement`.
Input descriptor is DSCSQLSTT/SQLDARD (input variant).

**Tasks:**
1. `SQLBindParameter` (input-only to start; INT/BIGINT/DOUBLE/CHAR/VARCHAR/
   DECIMAL/DATE/TIME/TIMESTAMP/NULL), `SQLNumParams`.
2. Build SQLDTA: describe params (DSCSQLSTT if needed), encode bound values in
   FD:OCA to match, send with EXCSQLSTT.
3. Handle NULL parameters via indicator; re-execute prepared statement with new
   values.

**Acceptance:** `INSERT ... VALUES(?,?,?)` executed multiple times with different
bound values inserts correct rows (verified by a follow-up SELECT). Parameterized
`SELECT ... WHERE id = ?` returns the right row.

**Checkpoint:** PROGRESS.md → Phase 12 DONE.

---

### Phase 13 — Transactions & connection attributes
**Goal:** Commit/rollback and autocommit control.

**Reference reading:** `NetConnectionRequest.java` `buildRDBCMM` (RDBCMM),
`buildRDBRLLBCK` (RDBRLLBCK), `writeLocalCommit/Rollback`; DRDA RDBCMTOK /
autocommit handling; ODBC `SQLEndTran`, `SQLSetConnectAttr(SQL_ATTR_AUTOCOMMIT)`.

**Tasks:**
1. `SQLSetConnectAttr` / `SQLGetConnectAttr` for `SQL_ATTR_AUTOCOMMIT`.
2. `SQLEndTran(SQL_COMMIT|SQL_ROLLBACK)` → RDBCMM / RDBRLLBCK flows, parse
   ENDUOWRM.
3. Ensure autocommit-off leaves a unit of work open across statements and
   commit/rollback closes it; default DRDA behavior is manual-commit — implement
   an autocommit-on wrapper that commits after each successful statement.

**Acceptance:** with autocommit OFF: INSERT then ROLLBACK leaves no row; INSERT
then COMMIT persists it (verified across a fresh connection). With autocommit ON:
INSERT persists without explicit commit.

**Checkpoint:** PROGRESS.md → Phase 13 DONE. Functional driver complete.

---

### Phase 14 — Non-blocking / nginx integration
**Goal:** Prove the protocol core runs without blocking, driven by an external
event loop (nginx-style).

**Reference reading:** none external — this is about your own `net`/`drda`
seam. Re-read your Phase 1 buffer design and Phase 5–9 flows.

**Tasks:**
1. Refactor (if not already) so every `drda_*` flow is expressible as: "write
   request bytes to out-buffer" + "advance a state machine as in-bytes arrive".
   Confirm no protocol code calls `net_recv` directly — it consumes from a
   buffer and returns `SDB2_NEED_MORE` when short.
2. Provide an async façade: `sdb2_async_*` (or a documented pattern) exposing the
   out-buffer FD-write and in-buffer FD-read hooks plus a `pump()` that returns
   need-read / need-write / done.
3. Non-blocking socket adapter (`O_NONBLOCK`, `EAGAIN` handling) alongside the
   blocking one.
4. Document the nginx usage contract in `ai-output/` notes or code comments.

**Acceptance:** a test harness using `poll()` and non-blocking sockets drives a
full connect→select→fetch→disconnect purely by pumping buffers, never blocking.
The blocking CLI path still works unchanged.

**Checkpoint:** PROGRESS.md → Phase 14 DONE.

---

### Phase 15 — Hardening, static-build verification, packaging
**Goal:** Ship-quality static artifact and test suite.

**Tasks:**
1. `ldd libstaticdb2-cli` shows "not a dynamic executable" (fully static musl).
2. Fill EBCDIC CCSID path (from Phase 2 stub) if a target host requires it; add a
   secmec upgrade path (EUSRIDPWD) if the test server rejects clear USRIDPWD.
3. Leak check (valgrind if available on glibc build variant), fuzz the DSS/FD:OCA
   parsers with truncated/garbage buffers — must never crash, always return an
   error.
4. Full test run via `sniprun` across all `test/*.in`; document env vars and how
   to run against a DB2 container (e.g. `icr.io/db2_community/db2`).
5. Update the project `README.md` "usage" section with a minimal API example.

**Acceptance:** static binary verified; full snip suite green; parser fuzz shows
no crashes; README example compiles and runs.

**Checkpoint:** PROGRESS.md → Phase 15 DONE. Project v1 complete.

---

## 5. Risk register (revisit each session)

- **Security mechanism**: modern DB2 LUW often refuses clear-text USRIDPWD.
  Phase 5 assumes a test instance allowing it; Phase 15 adds EUSRIDPWD
  (DES/Diffie-Hellman) if required. Configure the test DB2 with
  `SRVCON_AUTH`/`AUTHENTICATION` permitting USRIDPWD early to de-risk.
- **Package dependency**: EXCSQLIMM/PRPSQLSTT need a bound package. Rely on the
  DRDA default dynamic package Derby uses; if the server needs an explicit BIND,
  that becomes an extra sub-phase in Phase 8.
- **FD:OCA breadth**: decimal (packed/zoned) and datetime encodings are fiddly —
  keep tight fixtures. Reduce v1 type scope if needed and note it.
- **No DB2 in CI**: protocol layers (P3–P10) must be testable from committed byte
  fixtures. Capture fixtures from a Derby-vs-DB2 or `tcpdump` session and store
  under `test/fixtures/`.

## 6. Definition of done (v1)

Static musl `libstaticdb2.a` + CLI test binary that can, via the ODBC subset API,
connect to DB2 over DRDA, run parameterized queries and DML, iterate result sets
and read the single returned value, manage transactions, and be driven
non-blocking from an nginx-style event loop — with no dynamic linking and no IBM
CLI driver.

---

## Appendix A — `PROGRESS.md` template

Create `./ai-output/PROGRESS.md` at the start of Phase 1 and update it at every
checkpoint. A fresh session must be resumable from this file alone.

```markdown
# libstaticdb2 — PROGRESS

Last updated: <date/time>
Current phase: <N — name>
Overall status: <in-progress / blocked / done>

## Phase status
- [ ] P1  Scaffolding + buffer/net
- [ ] P2  CCSID conversion
- [ ] P3  DSS framing
- [ ] P4  DDM objects + codepoints
- [ ] P5  Connection handshake
- [ ] P6  SQLCA / diagnostics
- [ ] P7  FD:OCA descriptors
- [ ] P8  Non-query execution
- [ ] P9  Query execution + handles
- [ ] P10 Value decoding
- [ ] P11 ODBC public API (core)
- [ ] P12 Parameters
- [ ] P13 Transactions
- [ ] P14 Non-blocking / nginx
- [ ] P15 Hardening / packaging

## Files created so far
<path — one line each, what it contains>

## What works / verified
<bullet list, incl. which tests pass and whether against live DB2 or fixtures>

## In-progress detail (if stopped mid-phase)
- Phase: <N>
- Done in this phase: <tasks>
- NEXT CONCRETE STEP: <exact next action a fresh session should take>
- Open questions / blockers: <...>

## Environment notes
- DB2 test host reachable? <yes/no> ; secmec accepted: <USRIDPWD/EUSRIDPWD>
- Negotiated MGRLVLs from server (from P5): <...>
- Fixtures captured under test/fixtures/: <list>
```
