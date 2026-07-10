#!/bin/bash
# Creates the DB2 Docker container for libstaticdb2 integration testing.
# Run once; use db2-start.sh / db2-stop.sh for subsequent starts/stops.
#
# Requires ~/.env_list with at minimum:
#   DB2INSTANCE=db2inst1
#   DB2INST1_PASSWORD=db2inst1
#   LICENSE=accept
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=../local.env
source "${SCRIPT_DIR}/../local.env"

cd ~

echo "Creating container ${DB2_CONTAINER} (image ${DB2_IMAGE}) ..."
echo "Bind mount: ${DB2_BIND_MOUNT}"

docker run -h "${DB2_CONTAINER}" --name "${DB2_CONTAINER}" \
  --restart=always --detach --privileged=true \
  -p "${DB2_PORT}:50000" \
  --env-file ~/.env_list \
  -v "${DB2_BIND_MOUNT}:/database" \
     "${DB2_IMAGE}"

docker ps --all

# Fix a permissions bug in the container that causes the next boot to fail.
echo ""
echo "Waiting for DB2 to initialise (this takes several minutes on first run)..."
for i in $(seq 1 60); do
  sleep 10
  FENCED="${DB2_BIND_MOUNT}/config/${DB2_INST}/sqllib/adm/fencedid"
  if [[ -f "$FENCED" ]]; then
    sudo chgrp 1000 "$FENCED"
    echo "DB2 initialised (iteration ${i})."
    break
  fi
  echo "  ... still waiting (${i}/60)"
done

echo ""
echo "Container ${DB2_CONTAINER} started.  Next steps:"
echo "  1. docker logs -f ${DB2_CONTAINER}   # watch until 'Setup has completed'"
echo "  2. local-integration/bin/db2-create-testdb.sh"
