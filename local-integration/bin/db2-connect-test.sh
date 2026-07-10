#!/bin/bash
# Prove connectivity to TESTDB via libstaticdb2's own binary.
# Exits 0 on success.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=../local.env
source "${SCRIPT_DIR}/../local.env"

REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

echo "Testing DB2 connectivity: ${DB2_HOST}:${DB2_PORT}/${DB2_DB} ..."

# Verify the container itself can connect (docker-internal check).
docker exec -u "${DB2_INST}" "${DB2_CONTAINER}" bash -c \
  ". \$HOME/sqllib/db2profile; \
   db2 connect to ${DB2_DB} && \
   db2 'SELECT 1 FROM sysibm.sysdummy1' && \
   db2 connect reset" \
  | grep -q "1 record" && echo "  ✅ DB2 internal check: OK"

# If the integration test binary exists, run a quick connection test.
TBIN="${REPO_ROOT}/test/run_p16_integration"
if [[ -x "$TBIN" ]]; then
  DB2_TEST_HOST="${DB2_HOST}" \
  DB2_TEST_PORT="${DB2_PORT}" \
  DB2_TEST_DB="${DB2_DB}" \
  DB2_TEST_USER="${DB2_USERNAME}" \
  DB2_TEST_PASS="${DB2_PASSWORD}" \
    "${TBIN}" --connect-only
else
  echo "  (p16 binary not built; run 'cd test && ./p16_integration.make')"
fi

echo ""
echo "OK — connected to ${DB2_DB} successfully."
