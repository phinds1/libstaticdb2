#!/bin/bash
# Run db2delta integration tests against a local DB2 container.
# Usage:
#   ./run-db2delta-integration.sh           # use local.env defaults
#   DB2_TEST_HOST=x ./run-db2delta-integration.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
# shellcheck source=../local.env
source "${SCRIPT_DIR}/../local.env"

DB2_TEST_HOST="${DB2_TEST_HOST:-${DB2_HOST}}"
DB2_TEST_PORT="${DB2_TEST_PORT:-${DB2_PORT}}"
DB2_TEST_DB="${DB2_TEST_DB:-${DB2_DB}}"
DB2_TEST_USER="${DB2_TEST_USER:-${DB2_USERNAME}}"
DB2_TEST_PASS="${DB2_TEST_PASS:-${DB2_PASSWORD}}"
export DB2_TEST_HOST DB2_TEST_PORT DB2_TEST_DB DB2_TEST_USER DB2_TEST_PASS

# ── Ensure db2delta is built ───────────────────────────────────────────────
echo "=== Building db2delta ==="
cd "${REPO_ROOT}"
make db2delta -s

# ── Run snip integration test ──────────────────────────────────────────────
echo ""
echo "=== Running db2delta integration tests against ${DB2_TEST_HOST}:${DB2_TEST_PORT}/${DB2_TEST_DB} ==="
./snip/sniprun db2delta/test/d_integration.in
RC=$?

# ── End-to-end binary smoke tests ─────────────────────────────────────────
DB2DLT="${REPO_ROOT}/db2delta/db2delta"
if [[ -x "$DB2DLT" ]]; then
    echo ""
    echo "=== E2E smoke tests with binary ==="

    CS="-d ${DB2_TEST_HOST}:${DB2_TEST_PORT}/${DB2_TEST_DB} -u ${DB2_TEST_USER} -p ${DB2_TEST_PASS}"

    echo "  -V version query"
    ${DB2DLT} ${CS} -V && echo "  ✅ -V OK"

    echo "  -q SELECT 1"
    ${DB2DLT} ${CS} -q "SELECT 1 FROM SYSIBM.SYSDUMMY1" && echo "  ✅ -q OK"

    echo "  -s fixtures/sample.delta (first run — apply)"
    ${DB2DLT} ${CS} -s "${REPO_ROOT}/db2delta/test/fixtures/sample.delta" && echo "  ✅ apply OK"

    echo "  -s fixtures/sample.delta (second run — idempotent skip)"
    ${DB2DLT} ${CS} -s "${REPO_ROOT}/db2delta/test/fixtures/sample.delta" && echo "  ✅ idempotent OK"

    # Clean up test table
    ${DB2DLT} ${CS} -q "DROP TABLE users" 2>/dev/null || true
fi

if [[ $RC -eq 0 ]]; then
    echo ""
    echo "✅ All db2delta integration tests passed."
else
    echo ""
    echo "❌ db2delta integration tests FAILED (exit code ${RC})."
fi
exit $RC
