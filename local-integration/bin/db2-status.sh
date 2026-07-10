#!/bin/bash
# Show running status of the DB2 container.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=../local.env
source "${SCRIPT_DIR}/../local.env"

docker ps | grep "${DB2_CONTAINER}" || echo "Container ${DB2_CONTAINER} is not running."
