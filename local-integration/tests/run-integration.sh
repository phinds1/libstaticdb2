#!/bin/bash
# Run libstaticdb2 integration tests against a local DB2 container.
# Usage:
#   ./run-integration.sh                  # use local.env defaults
#   DB2_TEST_HOST=x ./run-integration.sh  # override host
#
# Requires: musl-gcc, docker (with DB2 container already running)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
# shellcheck source=../local.env
source "${SCRIPT_DIR}/../local.env"

# Allow env overrides
DB2_TEST_HOST="${DB2_TEST_HOST:-${DB2_HOST}}"
DB2_TEST_PORT="${DB2_TEST_PORT:-${DB2_PORT}}"
DB2_TEST_DB="${DB2_TEST_DB:-${DB2_DB}}"
DB2_TEST_USER="${DB2_TEST_USER:-${DB2_USERNAME}}"
DB2_TEST_PASS="${DB2_TEST_PASS:-${DB2_PASSWORD}}"

export DB2_TEST_HOST DB2_TEST_PORT DB2_TEST_DB DB2_TEST_USER DB2_TEST_PASS

# ── Build + Run via sniprun ────────────────────────────────────────────────────
echo "=== Running integration tests against ${DB2_TEST_HOST}:${DB2_TEST_PORT}/${DB2_TEST_DB} ==="
cd "${REPO_ROOT}"
./snip/sniprun test/p16_integration.in
RC=$?

if [[ $RC -eq 0 ]]; then
  echo ""
  echo "✅ All integration tests passed."
else
  echo ""
  echo "❌ Integration tests FAILED (exit code ${RC})."
fi
exit $RC
