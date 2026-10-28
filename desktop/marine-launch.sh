
#!/usr/bin/env bash
set -Eeuo pipefail

# 1. Resolve repository directory
REPO_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

START_SCRIPT="$REPO_DIR/desktop/marine-start.sh"
TUI_BIN="$REPO_DIR/controller/controller_tui"
ENV_FILE="$REPO_DIR/desktop/marine-ipc.env"

show_error() {
    echo
    echo "[ERROR] $1" >&2
    if [[ -t 0 ]]; then
        read -r -p "Press Enter to close..." || true
    fi
    exit 1
}

# 2. Load IPC configuration
[[ -r "$ENV_FILE" ]] || show_error "IPC configuration missing"

set -a
source "$ENV_FILE"
set +a

: "${MARINE_CONTROLLER_SOCKET:?}"
: "${XDG_RUNTIME_DIR:?Desktop session required}"

# 3. Check required executable files
[[ -x "$START_SCRIPT" ]] || show_error "marine-start.sh is not executable"
[[ -x "$TUI_BIN" ]] || show_error "Controller TUI binary is missing"

# 4. Start stopped programs
echo "[INFO] Checking marine sensor programs..."

if ! "$START_SCRIPT"; then
    echo "[WARN] One or more programs failed to start." >&2
    echo "[INFO] Opening TUI for status and log inspection."
fi

# Controller daemon must be available for IPC commands
if [[ ! -S "$MARINE_CONTROLLER_SOCKET" ]]; then
    show_error "Controller IPC socket is not ready"
fi

# 5. Prevent duplicate TUI windows
RUN_DIR="$XDG_RUNTIME_DIR/marine-sensors"
mkdir -p -- "$RUN_DIR"

exec 8>"$RUN_DIR/tui.lock"

if ! flock -n 8; then
    show_error "Controller TUI is already running"
fi

# 6. Launch Controller TUI
echo "[INFO] Opening Controller TUI..."

cd "$REPO_DIR/controller"
exec "$TUI_BIN"
