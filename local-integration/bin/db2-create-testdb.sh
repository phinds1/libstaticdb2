#!/bin/bash
# Creates the TESTDB database inside the container.
# Run once after the container is fully started
# (wait for "Setup has completed" in docker logs).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=../local.env
source "${SCRIPT_DIR}/../local.env"

DB_EXEC() {
  docker exec -u "${DB2_INST}" "${DB2_CONTAINER}" bash -c ". ~/.bashrc 2>/dev/null; . \$HOME/sqllib/db2profile; $1"
}

echo "=== Creating ${DB2_DB} database in ${DB2_CONTAINER} ==="
DB_EXEC "db2 CREATE DATABASE ${DB2_DB} AUTOMATIC STORAGE YES USING CODESET UTF-8 TERRITORY US PAGESIZE 32768"

echo "=== Tuning log config for local testing ==="
DB_EXEC "db2 update db cfg for ${DB2_DB} using logfilsiz 1024 logprimary 10 logsecond 20"

echo "=== Verifying database ==="
DB_EXEC "db2 list db directory | grep -A5 '${DB2_DB}'"

echo ""
echo "${DB2_DB} database created.  Run db2-connect-test.sh to verify."
