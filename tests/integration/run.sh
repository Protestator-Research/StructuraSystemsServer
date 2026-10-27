#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VENV="$REPO_ROOT/tests/.venv"

if [ ! -x "$VENV/bin/python" ]; then
    python3 -m venv "$VENV"
fi
"$VENV/bin/pip" install -q -r "$REPO_ROOT/tests/integration/requirements.txt"

cd "$REPO_ROOT"
exec "$VENV/bin/python" -m pytest tests/integration -v "$@"
