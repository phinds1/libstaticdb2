# db2delta

A lightweight, zero-dependency DB2 delta/migration tool — like Liquibase, but simple.

`db2delta` is a statically-compiled musl binary. It requires no Java, no XML, no
client libraries, and no `~db2inst1` home directory.

---

## Quick start

```bash
# Print tool version
db2delta version

# Apply all pending deltas in a directory
db2delta apply -d db2host:50000/MYDB -u db2inst1 -p mypass ./deltas/

# Run arbitrary SQL
db2delta exec -d db2host:50000/MYDB -u db2inst1 -p mypass "SELECT COUNT(*) FROM MYTABLE"
```

Or create `database.env` in the working directory and just run:

```bash
db2delta apply ./deltas/
```

---

## Usage

```
Usage: db2delta <subcommand> [options]

Subcommands:
  version                       Print db2delta version and exit
  exec   -d <dsn> -u <user> <SQL>    Execute SQL and print results
  apply  -d <dsn> -u <user> <file|dir>...   Apply delta files

Options (exec, apply):
  -d <host[:port]/database>     DB2 connection DSN (port defaults to 50000)
  -u <user>                     DB2 username
  -p <password>                 DB2 password (or set in database.env)
  -c, --continue-on-error       Do not abort on changeset error
  -x                            Script mode: minimal output, strict exit codes
```

Examples:

```bash
db2delta version
db2delta exec -d myhost/MYDB -u db2inst1 "SELECT 1 FROM SYSIBM.SYSDUMMY1"
db2delta apply -d myhost/MYDB -u db2inst1 changes/001_add_users.delta
db2delta apply -d myhost/MYDB -u db2inst1 -c migrations/
```

---

## Delta file format

Delta files use a simple `.ini`-inspired format. Files must end in `.delta`.

```ini
# Optional header — key=value pairs before the first section
terminator=;          # default ; — use @ for stored procedures
continue-on-error=false
author=Alice
logging=debug

[changeset:create_users_table]
CREATE TABLE users (
    id   INTEGER     NOT NULL,
    name VARCHAR(64)
);

[validation:users_table_exists]
SELECT COUNT(*) FROM syscat.tables WHERE tabname='USERS';
```

### Sections

| Section | Description |
|---------|-------------|
| `[changeset:NAME]` | SQL to apply (DDL, DML, etc.) |
| `[validation:NAME]` | Read-only SQL that checks the changeset worked |

### Header keys

| Key | Default | Description |
|-----|---------|-------------|
| `terminator` | `;` | Statement separator (use `@` for procedures that contain `;`) |
| `continue-on-error` | `false` | If `true`, errors do not abort the run |
| `author` | _(empty)_ | Stored in the changelog |
| `logging` | `warn` | `debug` \| `warn` \| `error` |

### File ordering

When a directory is passed to `apply`, files are ordered **ASCII-betically by
filename** (byte-wise `strcmp` on the basename). This means numeric prefixes sort
as strings: `10_x.delta` comes before `2_y.delta`. Use zero-padded dates or
sequence numbers for reliable ordering:

```
20240101_create_schema.delta
20240115_add_index.delta
20240201_insert_seed.delta
```

### Terminator note

Statement splitting is naive: the terminator character is split on everywhere in
the section body. If your SQL contains the terminator inside a string literal,
use `terminator=@` and end each statement with `@`.

---

## Changelog table

`db2delta` creates `DBDELTA_CHANGELOG` in the target database on first run.

```sql
CREATE TABLE DBDELTA_CHANGELOG (
    CHANGESET  VARCHAR(255) NOT NULL,
    FILENAME   VARCHAR(255) NOT NULL,
    APPLIED_AT TIMESTAMP    NOT NULL WITH DEFAULT CURRENT TIMESTAMP,
    SHA256     CHAR(64)     NOT NULL,
    AUTHOR     VARCHAR(255),
    PRIMARY KEY (CHANGESET)
);
```

Each applied changeset is recorded by name. Re-running `db2delta` over an
already-applied set is a **no-op** (exit 0).

The SHA256 column stores a hex-encoded SHA-256 of the canonical changeset SQL
(statements joined by `\n`).

### No rollback

`db2delta` is **forward-only**. There is no rollback mechanism. If a changeset
fails, already-applied changesets in the same run remain applied and recorded.
Use `--continue-on-error` to log errors and proceed, or fix the issue and re-run.

---

## database.env

Create `./database.env` in the working directory to avoid repeating connection
settings:

```dotenv
DSN=db2host:50000/MYDB
USER=db2inst1
PASSWORD=example-pass
CONTINUE_ON_ERROR=true
SCRIPT=false
```

`database.env` provides defaults. CLI flags always take precedence.

---

## Exit codes

| Code | Meaning |
|------|---------|
| `0` | All changesets applied or already present; all validations passed |
| `1` | At least one changeset or validation failed |
| `2` | Usage / argument error |

---

## Building from source

```bash
# From repo root — builds libstaticdb2.a then db2delta
make db2delta

# Run all unit tests (no DB required)
make db2delta-test

# Integration tests (requires DB2_TEST_HOST)
local-integration/tests/run-db2delta-integration.sh
```

Requires `musl-gcc` and `make`. The binary is fully static; copy it anywhere.

---

## Integration testing

Set up a local DB2 container:

```bash
local-integration/bin/db2-run.sh
local-integration/bin/db2-create-testdb.sh
local-integration/tests/run-db2delta-integration.sh
```

See `local-integration/README.md` for full setup instructions.
