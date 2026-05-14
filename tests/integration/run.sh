#!/usr/bin/env bash
# Host entry point for the Docker integration tests.
#
# Builds the image (cached after first run), starts progress-server + tester
# on a shared bridge network, exits with the tester's exit code.

set -euo pipefail

cd "$(dirname "$0")"

# Detect `docker compose` (v2) vs the legacy `docker-compose`.
if docker compose version >/dev/null 2>&1; then
    DC=(docker compose)
elif command -v docker-compose >/dev/null 2>&1; then
    DC=(docker-compose)
else
    echo "error: neither 'docker compose' nor 'docker-compose' is installed" >&2
    exit 2
fi

cleanup() {
    "${DC[@]}" down --remove-orphans --volumes >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "── Building image ──"
"${DC[@]}" build

echo "── Running integration suite ──"
"${DC[@]}" up --abort-on-container-exit --exit-code-from tester
