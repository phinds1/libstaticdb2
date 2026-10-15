# libstaticdb2 Local Integration Testing

Run libstaticdb2 integration tests against a local DB2 Community Edition container.
The integration tests exercise the full DRDA/DDM wire protocol against a real DB2 engine.

## Quick start

```bash
# 1. Create ~/.env_list (one-time setup)
cat > ~/.env_list <<EOF
DB2INSTANCE=db2inst1
DB2INST1_PASSWORD=db2inst1
LICENSE=accept
EOF

# 2. Create and start the DB2 container (~5 min first time)
local-integration/bin/db2-run.sh

# 3. Wait for "Setup has completed" then create the test database
docker logs -f db2server-libstaticdb2   # ctrl-c when ready
local-integration/bin/db2-create-testdb.sh

# 4. Verify connectivity
local-integration/bin/db2-connect-test.sh

# 5. Run integration tests
make integration
# or directly:
local-integration/tests/run-integration.sh
```

## Container lifecycle

| Command                                | Effect                               |
|----------------------------------------|--------------------------------------|
| `local-integration/bin/db2-run.sh`     | Create container (first time only)   |
| `local-integration/bin/db2-start.sh`   | Start stopped container              |
| `local-integration/bin/db2-stop.sh`    | Stop container (data preserved)      |
| `local-integration/bin/db2-status.sh`  | Check container running              |
| `local-integration/bin/db2-destroy.sh` | Remove container + data (full reset) |

## Environment variables

The `local.env` file supplies defaults.  Override any variable on the command line:

| Variable        | Default     | Description         |
|-----------------|-------------|---------------------|
| `DB2_TEST_HOST` | `127.0.0.1` | DB2 server hostname |
| `DB2_TEST_PORT` | `50000`     | DB2 DRDA port       |
| `DB2_TEST_DB`   | `TESTDB`    | Database name       |
| `DB2_TEST_USER` | `db2inst1`  | Username            |
| `DB2_TEST_PASS` | `db2inst1`  | Password            |

Example against a remote DB2:
```bash
DB2_TEST_HOST=mydb2.example.com DB2_TEST_DB=MYDB \
  DB2_TEST_USER=myuser DB2_TEST_PASS=secret \
  local-integration/tests/run-integration.sh
```

## Integration test inventory (test/p16_integration.in)

| Test | Description                                                  |
|------|--------------------------------------------------------------|
| I1   | `SQLDriverConnect` → `SQL_SUCCESS`                           |
| I2   | `CREATE TABLE` with INTEGER, VARCHAR, DOUBLE, DATE, DECIMAL  |
| I3   | `INSERT` 5 rows with mixed types; `SQLRowCount` = 1 each     |
| I4   | `SELECT *` ordered; bind all columns; verify id sum = 15     |
| I5   | `SQLDescribeCol` — names and types round-trip                |
| I6   | `SQLBindParameter` + prepared `INSERT`; row verified         |
| I7   | `ROLLBACK` — inserted row disappears                         |
| I8   | `COMMIT` — inserted row persists                             |
| I9   | NULL column round-trip — `SQL_NULL_DATA` indicator           |
| I10  | Large result set — 50 rows fetched without error             |
| I11  | `SQLGetData` on unbound VARCHAR column                       |
| I12  | Bad SQL → `SQL_ERROR`; `SQLCODE < 0` in `SQLGetDiagRec`      |
| I13  | `SQLRowCount` after `UPDATE` (3 rows) and `DELETE` (50 rows) |
| I14  | `SQLPrepare` + `SQLExecute` SELECT with parameterised WHERE  |
| I15  | `DROP TABLE` + verify table gone                             |
| I16  | `SQLDisconnect`                                              |

## Credentials (local dev only)

- DB2 username: `db2inst1`
- DB2 password: `db2inst1`

Do not use these credentials in production.
