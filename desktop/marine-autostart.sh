#!/usr/bin/env bash
set -Eeuo pipefail

REPO_DIR="$(
    cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." &&
    pwd -P
)"

LOG_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/marine-sensors/logs"

mkdir -p -- "$LOG_DIR"

{
    echo
    echo "========================================"
    echo "Marine Desktop Autostart"
    echo "Date: $(date -Is)"
    echo "========================================"

    echo "USER=$(id -un)"
    echo "XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-UNSET}"
    echo "XDG_CURRENT_DESKTOP=${XDG_CURRENT_DESKTOP:-UNSET}"

    if "$REPO_DIR/desktop/marine-start.sh"; then
        echo "[OK] Marine startup completed."
    else
        rc=$?
        echo "[ERROR] Marine startup returned code: $rc"
        exit "$rc"
    fi

} >> "$LOG_DIR/autostart.log" 2>&1
