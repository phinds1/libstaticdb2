#!/bin/bash
# Start an existing (stopped) DB2 container.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=../local.env
source "${SCRIPT_DIR}/../local.env"

docker start "${DB2_CONTAINER}"

echo "Container ${DB2_CONTAINER} starting.  Monitor with:"
echo "  docker logs -f ${DB2_CONTAINER}"
