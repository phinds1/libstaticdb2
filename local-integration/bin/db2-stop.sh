#!/bin/bash
# Stop the DB2 container (data preserved on bind mount).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=../local.env
source "${SCRIPT_DIR}/../local.env"

docker stop "${DB2_CONTAINER}"
echo "Container ${DB2_CONTAINER} stopped."
