#!/bin/bash
# fetch-references.sh — Download reference source repositories into ./references/
#
# These are read-only reference implementations used when developing
# libstaticdb2. They are NOT checked into git.
#
# Add references/ to .gitignore to keep them out of the repo.
#
# Usage:
#   ./fetch-references.sh          # download all
#   ./fetch-references.sh derby    # download one by name

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REF_DIR="$SCRIPT_DIR/references"

# ── Repo definitions ──────────────────────────────────────────────────────────
# Format: NAME  GIT_URL  TAG_OR_BRANCH  DEPTH
declare -A REPOS_URL REPOS_REF

REPOS_URL[go_ibm_db]="https://github.com/ibmdb/go_ibm_db.git"
REPOS_REF[go_ibm_db]="master"

REPOS_URL[python-ibmdb]="https://github.com/ibmdb/python-ibmdb.git"
REPOS_REF[python-ibmdb]="v3.2.9"

REPOS_URL[derby]="https://github.com/apache/derby.git"
REPOS_REF[derby]="trunk"

REPOS_URL[jtopen]="https://github.com/IBM/JTOpen.git"
REPOS_REF[jtopen]="v21.0.6"

# All known names in a fixed order
NAMES=(go_ibm_db python-ibmdb derby jtopen)

# ── Helper ────────────────────────────────────────────────────────────────────

fetch_one() {
    local name="$1"
    local url="${REPOS_URL[$name]}"
    local ref="${REPOS_REF[$name]}"
    local dest="$REF_DIR/$name"

    if [[ -d "$dest/.git" ]]; then
        echo "  [$name] already present — skipping (remove $dest to re-fetch)"
        return
    fi

    echo "  [$name] cloning $url @ $ref ..."
    # Shallow clone to keep it fast; depth=1 is enough for reference reading
    git clone --depth 1 --branch "$ref" "$url" "$dest"
    echo "  [$name] done."
}

# ── Main ──────────────────────────────────────────────────────────────────────

mkdir -p "$REF_DIR"

if [[ $# -gt 0 ]]; then
    # Fetch only the names passed on the command line
    for name in "$@"; do
        if [[ -z "${REPOS_URL[$name]+set}" ]]; then
            echo "error: unknown reference '$name'" >&2
            echo "known references: ${NAMES[*]}" >&2
            exit 1
        fi
        fetch_one "$name"
    done
else
    echo "Fetching all reference repositories into $REF_DIR ..."
    for name in "${NAMES[@]}"; do
        fetch_one "$name"
    done
fi

echo ""
echo "Done. Add 'references/' to .gitignore if not already present."
