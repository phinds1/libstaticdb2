#!/bin/bash
# Destroy the DB2 container and wipe the bind-mount data directory.
# WARNING: This deletes ALL database data.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=../local.env
source "${SCRIPT_DIR}/../local.env"

echo "Stopping and removing container ${DB2_CONTAINER} ..."
docker stop "${DB2_CONTAINER}" 2>/dev/null || true
docker rm   "${DB2_CONTAINER}" 2>/dev/null || true

echo "Removing bind-mount data at ${DB2_BIND_MOUNT} ..."
sudo rm -rf "${DB2_BIND_MOUNT}"

echo "Done — container and data removed."
