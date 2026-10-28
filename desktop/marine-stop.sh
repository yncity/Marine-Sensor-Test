
#!/usr/bin/env bash
set -Eeuo pipefail

# 1. Repository and IPC configuration
REPO_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_FILE="$REPO_DIR/desktop/marine-ipc.env"

if [[ ! -r "$ENV_FILE" ]]; then
    echo "[ERROR] IPC configuration not found: $ENV_FILE" >&2
    exit 1
fi

set -a
source "$ENV_FILE"
set +a

: "${MARINE_SONAR_SOCKET:?}"
: "${MARINE_DVL_SOCKET:?}"
: "${XDG_RUNTIME_DIR:?Run from a logged-in desktop session}"

RUN_DIR="$XDG_RUNTIME_DIR/marine-sensors"
mkdir -p -- "$RUN_DIR"

# 2. Prevent start/stop conflicts
exec 9>"$RUN_DIR/start.lock"
flock -x 9

# 3. Stop a verified process
stop_one() {
    local name="$1" binary="$2" sock="$3" signal="$4"
    local pidfile="$RUN_DIR/$name.pid"
    local pid="" actual="" expected=""
    local i

    expected="$(readlink -f -- "$binary")"

    if [[ ! -f "$pidfile" ]]; then
        if [[ -e "$sock" || -L "$sock" ]]; then
            echo "[ERROR] $name socket exists without PID file: $sock" >&2
            return 1
        fi

        echo "[INFO] $name already stopped."
        return 0
    fi

    read -r pid < "$pidfile" || true

    if [[ ! "$pid" =~ ^[0-9]+$ ]]; then
        echo "[ERROR] $name invalid PID file." >&2
        return 1
    fi

    if ! kill -0 "$pid" 2>/dev/null; then
        if [[ -e "$sock" || -L "$sock" ]]; then
            echo "[ERROR] $name stale socket requires inspection: $sock" >&2
            return 1
        fi

        rm -f -- "$pidfile"
        echo "[INFO] $name already stopped."
        return 0
    fi

    # Verify the PID belongs to the expected executable
    actual="$(readlink -f -- "/proc/$pid/exe" 2>/dev/null || true)"

    if [[ "$actual" != "$expected" ]]; then
        echo "[ERROR] $name PID does not match executable." >&2
        return 1
    fi

    # Request graceful shutdown
    if ! kill "-$signal" "$pid"; then
        echo "[ERROR] Failed to signal $name." >&2
        return 1
    fi

    # Wait up to 5 seconds
    for ((i=0; i<50; i++)); do
        if ! kill -0 "$pid" 2>/dev/null; then
            if [[ -e "$sock" || -L "$sock" ]]; then
                echo "[ERROR] $name exited but socket remains: $sock" >&2
                return 1
            fi

            rm -f -- "$pidfile"
            echo "[OK] $name stopped."
            return 0
        fi

        sleep 0.1
    done

    echo "[ERROR] $name did not stop cleanly." >&2
    return 1
}

# 4. Stop devices only
failures=0

# Servo: request graceful shutdown
stop_one servo \
    "$REPO_DIR/servo" \
    "$MARINE_SERVO_SOCKET" \
    TERM || ((failures+=1))

# DVL: use its existing SIGINT shutdown handler
stop_one dvl \
    "$REPO_DIR/dvl_test" \
    "$MARINE_DVL_SOCKET" \
    INT || ((failures+=1))

# Sonar: shutdown recording and scanning gracefully
stop_one sonar \
    "$REPO_DIR/sdkExample/build/bin/sdkExample" \
    "$MARINE_SONAR_SOCKET" \
    TERM || ((failures+=1))

# Keep controllerd and TUI running
if ((failures == 0)); then
    echo "[OK] All device stop requests completed."
else
    echo "[ERROR] Some devices failed to stop." >&2
    exit 1
fi
