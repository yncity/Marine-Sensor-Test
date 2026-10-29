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


: "${MARINE_IPC_DIR:?}"
: "${MARINE_SONAR_SOCKET:?}"
: "${MARINE_DVL_SOCKET:?}"
: "${MARINE_SERVO_SOCKET:?}"
: "${XDG_RUNTIME_DIR:?Run from a logged-in session}"

RUN_DIR="$MARINE_IPC_DIR"

if [[ ! -d "$RUN_DIR" || ! -w "$RUN_DIR" ]]; then
    echo "[ERROR] Runtime directory is missing or not writable: $RUN_DIR" >&2
    exit 1
fi

# 2. Prevent start/stop conflicts
exec 9>"$RUN_DIR/start.lock"
flock -x 9


# Check whether a process is still active (excluding zombies)
process_active() {
    local pid="$1"
    local stat_line="" stat_tail="" state=""

    if ! kill -0 "$pid" 2>/dev/null; then
        return 1
    fi

    # Check Linux process state
    if [[ -r "/proc/$pid/stat" ]]; then
        if ! IFS= read -r stat_line < "/proc/$pid/stat"; then
            return 0
        fi

        stat_tail="${stat_line##*) }"
        state="${stat_tail%% *}"

        case "$state" in
            Z|X|x)
                return 1
                ;;
        esac
    fi

    return 0
}


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

    if ! process_active "$pid"; then
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

    # Wait up to 5 seconds for process termination
    for ((i=0; i<50; i++)); do
        if ! process_active "$pid"; then
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
