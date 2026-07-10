# db2delta — Phased Implementation Plan

A forward-only DB2 delta/migration CLI built on `libstaticdb2.a`.
This document is written to be executed by an AI coding agent (Claude Sonnet 4.6)
without human assistance, one phase at a time.

---

## How to use this plan (READ THIS FIRST — every session)

This project is built in **10 small, sequential phases**. Each phase is sized to
fit comfortably in a single context window (~200–400 lines of new code). You will
almost never finish all phases in one session. The `PROGRESS.md` file is the
source of truth for **where you are**.

### Resume mechanism — do this at the start of every session

1. **Read `ai-output/PROGRESS.md`** if it exists.
   - It tells you which phases are DONE, which phase is NEXT, and records any
     design decisions or problems from earlier sessions.
   - If it does **not** exist, you are starting fresh at **Phase 1**. Create it
     as your first write (template at the bottom of this document).
2. **Read this plan's entry for the NEXT phase only.** Do not read ahead.
3. **Read the existing code the phase references** (paths are given explicitly).
4. **Implement the phase.**
5. **Run the phase's Verify step.** It must pass before you continue.
6. **Update `ai-output/PROGRESS.md`** (mark phase DONE, set NEXT, record decisions).
7. Stop, or continue to the next phase if context budget allows.

### Ground rules (apply to every phase)

- **Forward-only. There is NO rollback.** `db2delta` never undoes a changeset.
  If a changeset fails, we either stop (fail-fast, the default) or continue
  (`-c`/`--continue-on-error`). We never issue compensating/`DROP` DDL to reverse
  applied changes. Do not design, implement, or test a rollback path.
- **Idempotent by changelog.** A changeset that is already recorded in
  `DBDELTA_CHANGELOG` (matched by name) is skipped. Re-running the tool over an
  already-applied set of deltas is a no-op that exits 0.
- **Zero new dependencies.** Link only `libstaticdb2.a` and libc (musl). No
  OpenSSL, no third-party libs. SHA-256 is implemented in-tree (Phase 6).
- **Static musl build.** Everything compiles with `musl-gcc ... -static`, exactly
  like the parent library.
- **Match existing conventions.** Study the parent project's snip test format,
  Makefile style, and `test/p16_integration.in` before writing new code. Mirror
  them.
- **Every phase ships a test.** Prefer a snip test in `db2delta/test/`. Live DB2
  behaviour is covered by integration snips gated on `DB2_TEST_HOST`.

### Project layout being built

```
db2delta/
  Makefile              # sub-Makefile, builds ./db2delta binary + runs snip tests
  README.md             # user-facing docs (Phase 10)
  src/
    main.c              # entry point, arg parsing, dispatch
    args.c / args.h     # CLI + database.env arg model
    conn.c / conn.h     # connect / disconnect / version (-V)
    changelog.c/.h      # DBDELTA_CHANGELOG bootstrap + record/query
    delta.c / delta.h   # .delta file parser + in-memory model
    scan.c / scan.h     # directory scan + ASCII-betical ordering
    sha256.c / sha256.h # standalone SHA-256
    engine.c / engine.h # core execution loop (changesets + validation)
  test/
    d1_args.in / .make
    d2_conn.in / .make
    ... one snip per phase ...
    d_integration.in / .make   # live, gated on DB2_TEST_HOST
    fixtures/                    # sample .delta files
```

### Snip test mechanics (how tests are built and run)

The parent project uses a homegrown "snip" harness (`snip/sniprun`, `snip/snipcat`):

- A test is a pair: `NAME.in` (C source) + `NAME.make` (executable bash that
  compiles and runs it).
- `sniprun test/NAME.in` cd's into the test's directory, copies `NAME.in` to a
  file named `NAME` (stripping the extension; `//SNIP_FILE` directives are
  expanded by `snipcat`), then executes `./NAME.make`.
- The `.make` script compiles the copied `NAME` file with `musl-gcc ... -x c`,
  runs the resulting binary, and cleans it up. Exit non-zero = test fail.

Because `db2delta/test/` is **two** levels below the repo root, the `.make`
scripts reference the driver sources at `../../src/...` and headers at
`../../include`, and the sniprun binary lives at `../../snip/sniprun`. The
db2delta Makefile invokes `SNIPRUN = ../snip/sniprun` from inside `db2delta/`.

**Study these before writing your first `.make`:**
- `test/p11_odbc.make` — links a snip against the full driver source set + `-lm`.
- `test/p1_buf.make` — links a snip against a subset (`buf.c`, `net.c`).
- `test/p16_integration.make` — the live-DB2 link line.
- `snip/sniprun` — how `.in` → runnable file → `.make` works.

A db2delta snip test that needs driver symbols links the db2delta `src/*.c`
under test **plus** `libstaticdb2.a` (or the same driver `src/*.c` list used by
`p11_odbc.make`). Prefer linking the prebuilt `../../libstaticdb2.a` to keep the
link line short:

```bash
musl-gcc -std=c11 -Wall -Wextra -I ../../include -I ../src -static \
    -o run_dN_name -x c dN_name \
    ../src/thing_under_test.c \
    ../../libstaticdb2.a -lm
./run_dN_name
rm -f run_dN_name
```

(Confirm `../../libstaticdb2.a` exists — build it with `make lib` from repo root
if not. If a link error appears for a driver symbol, fall back to the explicit
`../../src/*.c` list from `p11_odbc.make`.)

### Existing infrastructure you will build on

- `libstaticdb2.a` — full ODBC subset: `SQLAllocHandle`, `SQLSetEnvAttr`,
  `SQLDriverConnect`, `SQLConnect`, `SQLExecDirect`, `SQLPrepare`, `SQLExecute`,
  `SQLBindParameter`, `SQLBindCol`, `SQLFetch`, `SQLGetData`, `SQLNumResultCols`,
  `SQLRowCount`, `SQLEndTran`, `SQLDisconnect`, `SQLFreeHandle`,
  `SQLGetDiagRec`, `SQLDescribeCol`.
- `include/staticdb2/sql.h` — ODBC constants/types (`SQL_SUCCESS`,
  `SQL_HANDLE_ENV`, `SQL_C_CHAR`, `SQL_NTS`, `SQL_DRIVER_NOPROMPT`, …).
- `include/staticdb2/sdb2.h` — `sdb2_err_t` codes, `sdb2_value_t`, `sdb2_ctype_t`.
- `src/buf.c/.h` — growable byte buffer utilities (reusable for building SQL/text).
- `test/p16_integration.in` — the live-DB2 test pattern (connection string, `exec`
  helper, `print_diag`, `CHECK`/`CHECK_EQ` macros). **Copy its idioms.**

Connection string format used by the driver (from `p16_integration.in`):
```
HOSTNAME=<host>;PORT=<port>;DATABASE=<db>;UID=<user>;PWD=<pass>
```

---

## Phase 1 — Scaffold + argument parsing

**Goal:** Create the `db2delta/` sub-project that compiles, links `libstaticdb2.a`,
parses all CLI flags, prints usage, and exits cleanly.

**Files to create:**
- `db2delta/Makefile`
- `db2delta/src/main.c`
- `db2delta/src/args.h`
- `db2delta/src/args.c`
- `db2delta/test/d1_args.in`
- `db2delta/test/d1_args.make`

**Implementation notes:**
- Define a `db2delta_args_t` struct in `args.h` holding: `dsn` (raw `-d`
  `host:port/database`), parsed `host`/`port`/`database`, `user` (`-u`),
  `password` (`-p`), `version_only` (`-V`), `query` (`-q`), `sql_files[]` +
  `sql_file_count` (`-s`), `script_mode` (`-x`), `continue_on_error` (`-c` /
  `--continue-on-error`), and a `mode` enum
  (`MODE_USAGE`, `MODE_VERSION`, `MODE_QUERY`, `MODE_APPLY`).
- Write `int args_parse(int argc, char **argv, db2delta_args_t *out)`. Return 0 on
  success, non-zero on parse error. Keep it a hand-rolled loop (no getopt
  dependency assumptions; getopt is fine but long-opt `--continue-on-error` needs
  `getopt_long` or manual handling — hand-rolling is simplest and dependency-free).
- Parse `-d host:port/database` into components with a small helper
  `args_parse_dsn(const char *dsn, ...)`. Port defaults are not assumed here; if
  missing, leave port = 0 and let Phase 2 default it (DB2 default 50000).
- `-s` consumes the remaining args as SQL/delta file paths (variadic, like the
  README: `-s sql_files...`).
- `main.c` calls `args_parse`; on `MODE_USAGE` or parse error, print a usage
  block to stderr and exit 2. For now, other modes just print the parsed args and
  exit 0 (real behaviour arrives in later phases). Keep `main.c` thin.
- Do **not** connect to a DB yet.

**Test (`d1_args.in`):** `#include "../src/args.h"` and `../src/args.c` compiled in.
Build several `argv[]` arrays and assert:
- `-d db2host:50000/TESTDB -u db2inst1 -V` → `mode == MODE_VERSION`, host/port/db
  parsed correctly.
- `-q "SELECT 1"` → `mode == MODE_QUERY`, `query` set.
- `-s a.delta b.delta` → `mode == MODE_APPLY`, `sql_file_count == 2`.
- `-x -c` flags set the booleans.
- missing `-d` (when required) → parse error / `MODE_USAGE`.
Use the parent's `CHECK` macro idiom (copy the small macro from `p1_buf.in`).

**Verify step:**
```
cd db2delta && make && ./db2delta || true    # prints usage, exits 2
cd db2delta && make test                      # runs d1_args snip → PASS
```

**End of phase:** Update `ai-output/PROGRESS.md` — mark Phase 1 DONE, set Phase 2
NEXT. Record: the final `db2delta_args_t` shape and any DSN-parsing decisions.

---

## Phase 2 — DB connection + `-V` version query

**Goal:** Connect to DB2 via `SQLDriverConnect`, and implement `-V` to print the
server version.

**Files to create/modify:**
- `db2delta/src/conn.h`
- `db2delta/src/conn.c`
- `db2delta/src/main.c` (wire `MODE_VERSION` → connect → query → print → disconnect)
- `db2delta/test/d2_conn.in`, `db2delta/test/d2_conn.make`
- `db2delta/test/d_integration.in`, `db2delta/test/d_integration.make` (start it here)

**Implementation notes:**
- `conn.h` exposes an opaque-ish `db2delta_conn_t` holding `SQLHENV`, `SQLHDBC`,
  and a reusable `SQLHSTMT`. Functions:
  - `int conn_open(db2delta_conn_t *, const db2delta_args_t *)`
  - `int conn_close(db2delta_conn_t *)`
  - `int conn_server_version(db2delta_conn_t *, char *out, size_t out_sz)`
- Build the connection string exactly as `p16_integration.in` does:
  `HOSTNAME=%s;PORT=%d;DATABASE=%s;UID=%s;PWD=%s`. Default port to 50000 if 0.
- Version query (per README):
  `SELECT SERVICE_LEVEL FROM TABLE(sysproc.env_get_inst_info()) AS T`.
  Bind column 1 as `SQL_C_CHAR`, `SQLFetch`, print the value.
- **Gotcha:** always check `rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO`. On
  failure, pull diagnostics with `SQLGetDiagRec` (copy `print_diag` from
  `p16_integration.in`) and return non-zero. Never leak handles — free env/dbc/stmt
  in `conn_close`, and free on every error path.
- `-V` mode: open → server_version → print `SERVICE_LEVEL` to stdout → close →
  exit 0. On connect failure exit 1.

**Test:**
- Unit snip `d2_conn.in`: exercise the **connection-failure path** without a live
  DB — call `conn_open` against an unroutable host/port (e.g.
  `127.0.0.1:1`), assert it returns non-zero and does not crash / leaks no handle
  (function returns cleanly). Keep the timeout short if the driver supports it;
  otherwise document that this test may block briefly and pick a port that RSTs
  fast (`127.0.0.1:1`).
- Integration snip `d_integration.in`: follow `p16_integration.in` — read
  `DB2_TEST_HOST` (+ PORT/DB/USER/PASS); if unset, print "skipped" and exit 0.
  When set, `conn_open` → `conn_server_version` → assert non-empty string → close.

**Verify step:**
```
cd db2delta && make test           # d2_conn (failure path) PASS
# Live (optional):
DB2_TEST_HOST=<host> ../snip/sniprun test/d_integration.in
```

**End of phase:** Update `PROGRESS.md` — Phase 2 DONE, Phase 3 NEXT. Record the
connection-string format and how `-V` output is formatted.

---

## Phase 3 — Changelog table bootstrap

**Goal:** Ensure the `DBDELTA_CHANGELOG` tracking table exists, and provide
functions to record and query applied changesets.

**Files to create/modify:**
- `db2delta/src/changelog.h`
- `db2delta/src/changelog.c`
- `db2delta/test/d3_changelog.in`, `db2delta/test/d3_changelog.make`
- `db2delta/test/d_integration.in` (add changelog I-cases)

**Implementation notes:**
- Table DDL:
  ```sql
  CREATE TABLE DBDELTA_CHANGELOG (
      CHANGESET   VARCHAR(255) NOT NULL,
      FILENAME    VARCHAR(255) NOT NULL,
      APPLIED_AT  TIMESTAMP    NOT NULL DEFAULT CURRENT TIMESTAMP,
      SHA256      CHAR(64)     NOT NULL,
      AUTHOR      VARCHAR(255),
      PRIMARY KEY (CHANGESET)
  );
  ```
- **DB2 gotcha — no portable `CREATE TABLE IF NOT EXISTS`.** DB2 gained
  `CREATE TABLE IF NOT EXISTS` only in recent 11.5 mod packs and it is not
  reliable across versions. **Do not rely on it.** Instead, check existence first:
  ```sql
  SELECT COUNT(*) FROM SYSCAT.TABLES
   WHERE TABNAME = 'DBDELTA_CHANGELOG' AND TABSCHEMA = CURRENT SCHEMA;
  ```
  If count == 0, run the `CREATE TABLE`. Wrap this in
  `int changelog_ensure(db2delta_conn_t *)`.
- Note: `SYSCAT.TABLES.TABNAME`/`TABSCHEMA` store **uppercase** unqualified names.
  Compare uppercase. Restrict to `CURRENT SCHEMA` to avoid matching other schemas.
- Functions:
  - `int changelog_ensure(conn)` — create if absent (idempotent).
  - `int changelog_has(conn, const char *changeset, int *out_present)` —
    `SELECT COUNT(*) ... WHERE CHANGESET = ?` via `SQLBindParameter`.
  - `int changelog_record(conn, changeset, filename, sha256_hex, author)` —
    parameterized `INSERT`.
- Commit after DDL/insert if autocommit is off; the parent driver defaults are
  documented in `handles.c` — check `SQLGetConnectAttr`/autocommit. Simplest:
  leave autocommit on (driver default) so DDL commits implicitly; verify in the
  integration test.

**Test:**
- Unit snip `d3_changelog.in`: no live DB. Expose the SQL-builder helpers as small
  pure functions (e.g. `changelog_ddl_sql(char *buf, size_t)`,
  `changelog_exists_sql(...)`, `changelog_insert_sql(...)`) and assert the emitted
  SQL text is well-formed: contains `DBDELTA_CHANGELOG`, the four required columns,
  a `PRIMARY KEY`, and a `?` placeholder in the insert. This keeps the unit test
  DB-free while still exercising real code.
- Integration cases (in `d_integration.in`, gated): `changelog_ensure` twice in a
  row succeeds (idempotent), `changelog_record` then `changelog_has` returns
  present == 1, unknown changeset returns present == 0.

**Verify step:**
```
cd db2delta && make test
DB2_TEST_HOST=<host> ../snip/sniprun test/d_integration.in   # optional live
```

**End of phase:** Update `PROGRESS.md` — Phase 3 DONE, Phase 4 NEXT. Record the
final table schema and the `SYSCAT.TABLES` existence-check decision.

---

## Phase 4 — Delta file parser

**Goal:** Parse the `.delta` file format into an in-memory model.

**Files to create/modify:**
- `db2delta/src/delta.h`
- `db2delta/src/delta.c`
- `db2delta/test/d4_delta.in`, `db2delta/test/d4_delta.make`
- `db2delta/test/fixtures/sample.delta` (test fixture)

**Implementation notes:**
- File grammar (from README):
  - Leading **header** lines `key=value` until the first `[` section: keys
    `terminator` (default `;`), `continue-on-error` (default `false`), `logging`,
    `author`. Unknown keys: ignore but don't error (log at debug).
  - Sections: `[changeset:NAME]` and `[validation:NAME]`.
  - Section body is SQL, possibly multiple statements separated by the configured
    `terminator` (default `;`, override `@` e.g. for stored procedures).
  - Comment lines start with `#` (as in the README header example) — strip them.
- In-memory model in `delta.h`:
  ```c
  typedef enum { DELTA_CHANGESET, DELTA_VALIDATION } delta_kind_t;
  typedef struct { delta_kind_t kind; char name[256];
                   char **stmts; int stmt_count; } delta_section_t;
  typedef struct { char terminator; int continue_on_error;
                   char author[256]; char logging[32];
                   delta_section_t *sections; int section_count;
                   char filename[256]; } delta_file_t;
  ```
- API:
  - `int delta_parse_file(const char *path, delta_file_t *out)`
  - `int delta_parse_string(const char *text, const char *filename, delta_file_t *out)`
    (parse-from-string enables a DB-free unit test)
  - `void delta_free(delta_file_t *)`
- **Terminator splitting gotcha:** split the section body on the terminator char,
  trim whitespace, and **drop empty trailing statements** (a trailing `;` yields an
  empty final chunk). Do NOT split inside string literals — for v1, keep it simple
  and document the limitation: naive split is acceptable because migration DDL/DML
  rarely embeds the terminator in a literal; note this in the README (Phase 10).
- Preserve section order as they appear in the file (the file itself is authored in
  ASCII-betical intent, but ordering across files is Phase 5).

**Test (`d4_delta.in`):** parse `fixtures/sample.delta` (create it mirroring the
README example: header block + `[changeset:create_users_table]` +
`[validation:users_table_exists]`). Assert:
- `terminator == ';'`, `continue_on_error == 0`, `author == "Alice"`.
- exactly 2 sections; names `create_users_table` and `users_table_exists`;
  kinds CHANGESET then VALIDATION.
- the changeset has exactly 1 statement whose text contains `CREATE TABLE users`.
- Add a second fixture or inline string with `terminator=@` and a multi-statement
  procedure body to verify `@` splitting and that `;` inside the body is preserved.

**Verify step:** `cd db2delta && make test`

**End of phase:** Update `PROGRESS.md` — Phase 4 DONE, Phase 5 NEXT. Record the
grammar decisions (comment handling, empty-statement trimming, naive terminator
split limitation).

---

## Phase 5 — Directory scanner + ASCII-betical ordering

**Goal:** Given a directory (or list of paths), collect `*.delta` files and order
them ASCII-betically to produce a deterministic apply order.

**Files to create/modify:**
- `db2delta/src/scan.h`
- `db2delta/src/scan.c`
- `db2delta/test/d5_scan.in`, `db2delta/test/d5_scan.make`
- `db2delta/test/fixtures/scandir/` (a few empty-ish `.delta` files)

**Implementation notes:**
- API:
  - `int scan_dir(const char *dir, char ***out_paths, int *out_count)` — uses
    `opendir`/`readdir`, filters names ending in `.delta`, builds a sorted array.
  - `int scan_sort(char **paths, int count)` — pure, sorts an existing list. Expose
    this separately so the unit test can sort a **mock** list without touching the
    filesystem.
  - `void scan_free(char **paths, int count)`.
- **Ordering = plain byte-wise `strcmp`** ("ASCII-betical"). Do **not** use
  `strcoll`/locale collation — order must be stable and locale-independent. Sort by
  basename? Decision: sort by the **full path string** is ambiguous across dirs; for
  a single scanned directory sort by **basename** via `strcmp`. Document the choice.
- Skip subdirectories (only regular files). Skip dotfiles.
- If a path passed on the CLI is a file (not a dir), include it directly; if it is a
  dir, scan it. This lets `-s dir/` and `-s a.delta b.delta` both work.

**Test (`d5_scan.in`):** call `scan_sort` on a mock array
`{"20_b.delta","10_a.delta","02_x.delta","10_a.delta"}`-style input and assert the
result is byte-wise ascending (`02_x`, `10_a`, `20_b`). Also verify a numeric-prefix
convention sorts as strings (`10` before `2` unless zero-padded) — assert this
explicitly so the ASCII rule is documented by the test. Optionally, if `fixtures/
scandir/` exists, call `scan_dir` and assert only `.delta` files are returned in
order.

**Verify step:** `cd db2delta && make test`

**End of phase:** Update `PROGRESS.md` — Phase 5 DONE, Phase 6 NEXT. Record the
sort-key decision (basename, byte-wise `strcmp`) and the string-vs-numeric ordering
caveat for users.

---

## Phase 6 — SHA-256 (standalone, in-tree)

**Goal:** Compute a SHA-256 hex digest of changeset SQL text, with no external
crypto dependency.

**Files to create/modify:**
- `db2delta/src/sha256.h`
- `db2delta/src/sha256.c`
- `db2delta/test/d6_sha256.in`, `db2delta/test/d6_sha256.make`

**Implementation notes:**
- Implement the standard FIPS 180-4 SHA-256 (~100–150 lines). **Do NOT link
  OpenSSL** — the static musl build avoids it.
- API:
  - `void sha256(const uint8_t *data, size_t len, uint8_t out32[32])`
  - `void sha256_hex(const uint8_t *data, size_t len, char out_hex[65])` — writes
    lowercase hex + NUL.
- Optionally provide an init/update/final streaming API, but one-shot is enough for
  changeset text.
- Be careful with the length padding (big-endian 64-bit bit length) and word
  rotations. Use `uint32_t` and `uint64_t` explicitly.
- **Canonicalization decision:** hash the changeset's SQL exactly as parsed and
  re-serialized. Define a single canonical form (e.g. statements joined by `\n`
  after trimming) so the hash is stable across reformatting-insensitive
  whitespace. Document the exact bytes hashed — the changelog `SHA256` column
  depends on it and must be reproducible.

**Test (`d6_sha256.in`):** assert known vectors:
- `sha256_hex("", 0)` ==
  `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`
- `sha256_hex("abc", 3)` ==
  `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`
- one longer multi-block vector (e.g. the 56-byte
  `"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"`) ==
  `248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1`.

**Verify step:** `cd db2delta && make test`

**End of phase:** Update `PROGRESS.md` — Phase 6 DONE, Phase 7 NEXT. Record the
canonicalization rule (exactly which bytes are hashed).

---

## Phase 7 — Core execution engine

**Goal:** For each ordered `.delta` file and each changeset in order: skip if already
in the changelog, else execute its SQL statements and record it. Honour
fail-fast vs `continue-on-error`.

**Files to create/modify:**
- `db2delta/src/engine.h`
- `db2delta/src/engine.c`
- `db2delta/src/main.c` (wire `MODE_APPLY` and `MODE_QUERY`)
- `db2delta/test/d7_engine.in`, `db2delta/test/d7_engine.make`
- `db2delta/test/d_integration.in` (add end-to-end apply case)

**Implementation notes:**
- Main entry: `int engine_apply(db2delta_conn_t *, const db2delta_args_t *)`:
  1. `changelog_ensure`.
  2. Build ordered file list via `scan` from `args->sql_files`.
  3. For each file: `delta_parse_file`.
  4. For each `DELTA_CHANGESET` section (skip validations here — Phase 8):
     - Compute `sha256_hex` over the canonical changeset SQL.
     - `changelog_has(name)` → if present, log "skip" and continue.
     - Else execute each statement with `SQLExecDirect`; on error capture
       diagnostics.
     - On success: `changelog_record(name, filename, sha, author)`.
- **`-q` arbitrary SQL** (`MODE_QUERY`): execute the query; if it returns a result
  set, print rows (tab/space separated, using `SQLNumResultCols` +
  `SQLDescribeCol` + `SQLGetData` per column, like `p16_integration.in`). No
  changelog interaction for `-q`.
- **Fail-fast (default):** on the first statement error, stop, return non-zero, do
  **not** record the failing changeset. Remember: **no rollback** — already-applied
  changesets from earlier in the run stay applied and recorded.
- **`continue-on-error`** (`-c` CLI, or `continue-on-error=true` file header): log
  the error, skip recording the failing changeset, continue to the next. The
  effective flag is `CLI -c OR file-header continue-on-error`. Track whether any
  changeset failed (needed for exit codes in Phase 9).
- Return an `engine_result_t` (applied count, skipped count, failed count) so
  Phase 9 can map it to exit codes.

**Test:**
- Unit snip `d7_engine.in`: DB-free test of the decision logic. Refactor the
  skip/execute/record decision into a testable pure function, e.g.
  `engine_decide(int already_present, int stmt_failed, int continue_on_error)` →
  enum `{APPLY_RECORD, SKIP_PRESENT, STOP_FAIL, CONTINUE_FAIL}`. Assert the truth
  table: present→SKIP; not-present+ok→APPLY_RECORD; not-present+fail+failfast→STOP;
  not-present+fail+continue→CONTINUE_FAIL.
- Integration (gated): apply `fixtures/sample.delta` against live DB2, assert the
  table is created and the changeset appears in `DBDELTA_CHANGELOG`; re-run and
  assert it is skipped (idempotent, applied count 0 the second time).

**Verify step:**
```
cd db2delta && make test
DB2_TEST_HOST=<host> ../snip/sniprun test/d_integration.in
```

**End of phase:** Update `PROGRESS.md` — Phase 7 DONE, Phase 8 NEXT. Record the
`engine_result_t` shape and the fail-fast/continue semantics, and re-state
NO-ROLLBACK behaviour explicitly.

---

## Phase 8 — Validation blocks

**Goal:** Execute `[validation:NAME]` SQL and pass/fail based on its result.

**Files to create/modify:**
- `db2delta/src/engine.c` / `engine.h` (extend)
- `db2delta/test/d8_validation.in`, `db2delta/test/d8_validation.make`
- `db2delta/test/d_integration.in` (add validation case)

**Implementation notes:**
- Validation semantics: a validation **passes** if its query returns at least one
  row **and** the first column of the first row is non-zero / non-empty.
  - Numeric first column (e.g. `SELECT COUNT(*) ...`): pass if value `!= 0`.
  - Character first column: pass if non-empty (`len > 0`).
  - No rows returned: **fail**.
- Where validations run:
  - Inline within a file: a `[validation:...]` that appears **after** a
    `[changeset:...]` is treated as a post-condition of that changeset — run it
    immediately after the changeset applies successfully. (A validation with no
    preceding changeset in the file runs standalone in file order.)
  - Validations are **not** recorded in the changelog (they are checks, not
    migrations) — they re-run every invocation. Document this.
- Function: `int engine_validate(db2delta_conn_t *, const delta_section_t *,
  int *out_pass)`. Use `SQLExecDirect` + `SQLFetch` + `SQLGetData` on column 1.
- On validation failure: treat like a changeset error for exit-code purposes
  (fail-fast stops; `continue-on-error` logs and continues). Report clearly:
  `VALIDATION FAILED: <name>`.

**Test (`d8_validation.in`):** DB-free test of the **decision** function. Refactor
the pass/fail interpretation into a pure helper
`validation_passes(const sdb2_value_t *first_col, int had_row)` and assert:
- had_row=0 → fail.
- integer 0 → fail; integer 5 → pass.
- empty string → fail; "x" → pass.
Integration (gated): run the `users_table_exists` validation from the fixture after
applying the table; assert pass. Then run a validation expected to return 0 and
assert fail is reported.

**Verify step:**
```
cd db2delta && make test
DB2_TEST_HOST=<host> ../snip/sniprun test/d_integration.in
```

**End of phase:** Update `PROGRESS.md` — Phase 8 DONE, Phase 9 NEXT. Record the
validation pass/fail rule and that validations are not recorded in the changelog.

---

## Phase 9 — `database.env` support + `-x` scripting mode / exit codes

**Goal:** Load default args from `./database.env` so a bare `db2delta` works in a
configured directory, and define exit codes for `-x` scripting mode.

**Files to create/modify:**
- `db2delta/src/args.c` / `args.h` (extend)
- `db2delta/src/main.c` (apply exit-code policy)
- `db2delta/test/d9_env.in`, `db2delta/test/d9_env.make`
- `db2delta/test/fixtures/database.env`

**Implementation notes:**
- `database.env` format: simple `KEY=value` lines, `#` comments, blank lines
  ignored. Recognised keys map to CLI args, e.g.:
  ```
  DSN=db2host:50000/TESTDB
  USER=db2inst1
  PASSWORD=secret
  CONTINUE_ON_ERROR=false
  SCRIPT=true
  ```
  (Choose key names and document them; keep them stable.)
- **Precedence:** explicit CLI flags override `database.env` values, which override
  built-in defaults. Implementation: load `database.env` into the args struct
  first, then run `args_parse` on `argv` allowing it to overwrite. Expose
  `int args_load_env(const char *path, db2delta_args_t *out)` so it is unit
  testable.
- Only auto-load `./database.env` if it exists; absence is not an error.
- **Exit codes** (README: `-x` for scripting):
  - `0` — all requested work succeeded (all changesets applied or already present;
    all validations passed). A fully idempotent no-op run is `0`.
  - `1` — at least one changeset failed to apply, or at least one validation failed,
    or (in strict scripting) at least one changeset was **skipped-because-failed**.
    Define: in `-x` mode, exit 1 if `failed_count > 0`. Skips due to
    already-applied do **not** cause exit 1 (that is the normal idempotent case).
  - `2` — usage / argument error (already used in Phase 1).
  - Non-`-x` mode may still return these codes; `-x` primarily guarantees quiet,
    machine-friendly output and the strict 0/1 contract. Document the mapping.
- In `-x` mode reduce chatter: print only errors + a terse summary line; no
  decorative logging.

**Test (`d9_env.in`):** write/point at `fixtures/database.env`, call
`args_load_env`, assert keys populate the struct. Then simulate CLI override:
load env (DSN=A) then `args_parse(["-d","B..."])` and assert CLI value wins. Add a
pure `int compute_exit_code(const engine_result_t *, int script_mode)` and assert
the truth table (all-ok→0, one-fail→1, idempotent-skip→0).

**Verify step:** `cd db2delta && make test`

**End of phase:** Update `PROGRESS.md` — Phase 9 DONE, Phase 10 NEXT. Record the
`database.env` key names, precedence order, and the exact exit-code contract.

---

## Phase 10 — Full integration, Makefile wiring, README

**Goal:** Wire `db2delta` into the top-level build, add a live integration runner,
and write user-facing docs.

**Files to create/modify:**
- `Makefile` (repo root) — add a `db2delta` target that builds the sub-project
- `db2delta/Makefile` — ensure `all`, `test`, `clean` targets are complete
- `local-integration/tests/run-db2delta-integration.sh`
- `db2delta/README.md`
- `db2delta/test/d_integration.in` (final end-to-end pass)

**Implementation notes:**
- Top-level `Makefile`: add
  ```make
  .PHONY: db2delta db2delta-test
  db2delta: lib
  	$(MAKE) -C db2delta
  db2delta-test: db2delta
  	$(MAKE) -C db2delta test
  ```
  and mention it in `all` if appropriate (keep `lib` as the default). The
  sub-Makefile links `../libstaticdb2.a`; ensure the root `db2delta` target depends
  on `lib` so the archive exists first.
- `db2delta/Makefile`: compile `src/*.c` → `db2delta` binary, link
  `../libstaticdb2.a -lm -static`; `test` runs every `dN_*.in` snip via
  `../snip/sniprun`; `clean` removes objects, the binary, and stray `run_*` files.
- Integration runner: mirror `local-integration/tests/run-integration.sh` — source
  `local.env`, export `DB2_TEST_*`, run `../snip/sniprun db2delta/test/
  d_integration.in`, and additionally run the built binary end-to-end against the
  live container: apply a fixture delta dir, assert exit 0, re-run assert exit 0
  (idempotent), run a `-V` and a `-q "SELECT 1 FROM SYSIBM.SYSDUMMY1"`.
- `db2delta/README.md`: document usage (all flags from the project README), the
  `.delta` file format, `database.env`, the changelog table, exit codes, the
  **no-rollback / forward-only** guarantee, and the known parser limitation
  (terminator inside string literals).

**Test / Verify step:**
```
# From repo root:
make db2delta            # builds lib + db2delta binary, zero warnings
make db2delta-test       # all dN snip tests PASS
# Live end-to-end (optional):
local-integration/tests/run-db2delta-integration.sh
```

**End of phase:** Update `PROGRESS.md` — Phase 10 DONE, project COMPLETE. Record
the final Makefile targets, the integration runner path, and a short "what works"
summary.

---

## Testing strategy (summary)

- **Unit (snip, DB-free):** every phase ships at least one `dN_*.in` + `.make`
  snip in `db2delta/test/`, run by `../snip/sniprun`. Keep logic testable without a
  DB by factoring pure decision/SQL-builder functions (args parsing, SQL text
  generation, sort order, SHA-256 vectors, engine decision table, validation
  interpretation, env precedence, exit-code mapping).
- **Integration (snip, live):** a single growing `db2delta/test/d_integration.in`
  follows the `test/p16_integration.in` pattern — it **skips and exits 0** unless
  `DB2_TEST_HOST` is set. Run via
  `local-integration/tests/run-db2delta-integration.sh` against the Docker DB2 in
  `local-integration/`.
- **No rollback tests** — there is no rollback path to test. Idempotency
  (re-apply = no-op, exit 0) is the key end-to-end property to verify instead.
- Always build with `-Wall -Wextra` and keep the build warning-free, matching the
  parent library's standard.

---

## PROGRESS.md template (create on first run if absent)

```markdown
# db2delta — PROGRESS

Last updated: <date>
Current phase: <e.g. Phase 3 in progress>
Next phase: <e.g. Phase 3>
Overall status: <in progress | COMPLETE>

## Phase status
- [x] P1  Scaffold + arg parsing
- [x] P2  Connection + -V version
- [x] P3  Changelog table bootstrap
- [x] P4  Delta file parser
- [x] P5  Directory scan + ordering
- [x] P6  SHA-256
- [x] P7  Core execution engine
- [x] P8  Validation blocks
- [x] P9  database.env + -x exit codes
- [x] P10 Integration + Makefile wiring + README — COMPLETE

## Files created so far
- <path> — <one-line description>

## Design decisions
- <decision + rationale, e.g. "DSN parsed as host:port/db; port defaults to 50000">

## Problems / gotchas encountered
- <problem + resolution or open question>

## What works / verified
- <e.g. "make test: d1..d3 PASS; -V verified live against DB2 11.5">
```

At the end of **every** phase: mark the phase `[x]`, set *Next phase*, append any
new files, design decisions, and gotchas. This file is how the next session (or a
fresh agent with no memory) resumes exactly where you left off.
