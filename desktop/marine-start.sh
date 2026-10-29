
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

# Controller daemon uses this script for the q command
export MARINE_STOP_SCRIPT="$REPO_DIR/desktop/marine-stop.sh"

: "${MARINE_SONAR_SOCKET:?}"
: "${MARINE_DVL_SOCKET:?}"
: "${MARINE_SERVO_SOCKET:?}"
: "${MARINE_CONTROLLER_SOCKET:?}"
: "${XDG_RUNTIME_DIR:?Run from a logged-in desktop session}"

# 2. Prepare runtime and log directories
IPC_DIR="$(dirname -- "$MARINE_CONTROLLER_SOCKET")"

if [[ ! -d "$IPC_DIR" || ! -w "$IPC_DIR" ]]; then
    echo "[ERROR] IPC directory is missing or not writable: $IPC_DIR" >&2
    exit 1
fi

RUN_DIR="$XDG_RUNTIME_DIR/marine-sensors"
LOG_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/marine-sensors/logs"

# Controller CLI for IPC readiness checks
CONTROLLER_CLI="$REPO_DIR/controller/controller"

if [[ ! -x "$CONTROLLER_CLI" ]]; then
    echo "[ERROR] Controller CLI binary not found: $CONTROLLER_CLI" >&2
    exit 1
fi

mkdir -p -- "$RUN_DIR" "$LOG_DIR"
chmod 700 "$RUN_DIR"

# Prevent simultaneous startup requests
exec 9>"$RUN_DIR/start.lock"
flock -x 9


# Check real IPC responses through controllerd
ipc_ready() {
    local name="$1"
    local command="" prefix="" reply=""

    case "$name" in
        controllerd)
            command="ping"
            prefix="PONG: controllerd is responding"
            ;;
        sonar)
            command="sonar-health"
            prefix="SONAR STATE:"
            ;;
        dvl)
            command="dvl-health"
            prefix="DVL STATE:"
            ;;
        servo)
            command="servo-health"
            prefix="SERVO STATE:"
            ;;
        *)
            return 1
            ;;
    esac

    reply="$(timeout 3 "$CONTROLLER_CLI" "$command" 2>/dev/null)" || return 1

    [[ "$reply" == "$prefix"* ]]
}


# 3. Start a program only if it is not running
start_one() {
    local name="$1" binary="$2" workdir="$3" sock="$4"
    local pidfile="$RUN_DIR/$name.pid"
    local logfile="$LOG_DIR/$name.log"
    local old_pid="" actual="" expected=""
    local i
    shift 4

    if [[ ! -x "$binary" ]]; then
        echo "[ERROR] $name executable not found: $binary" >&2
        return 1
    fi

    expected="$(readlink -f -- "$binary")"

    # Check previously recorded PID
    if [[ -f "$pidfile" ]]; then
        read -r old_pid < "$pidfile" || true

        if [[ "$old_pid" =~ ^[0-9]+$ ]] &&
           kill -0 "$old_pid" 2>/dev/null; then

            actual="$(readlink -f -- "/proc/$old_pid/exe" 2>/dev/null || true)"

            if [[ "$actual" == "$expected" ]]; then
                if [[ -S "$sock" ]] && ipc_ready "$name"; then
                    echo "[OK] $name already running (PID $old_pid), IPC responding"
                    return 0
                fi

                echo "[WARN] $name is running but IPC is not responding: $sock" >&2
                return 1
            fi
        fi

        rm -f -- "$pidfile"
    fi

    # Never remove an existing IPC socket automatically
    if [[ -e "$sock" ]]; then
        echo "[ERROR] $name socket already exists: $sock" >&2
        echo "Check the running process or stale socket." >&2
        return 1
    fi

    # Launch in background and redirect output
    (
        cd -- "$workdir"
        exec nohup "$binary" "$@" </dev/null >>"$logfile" 2>&1 9>&-
    ) &

    local pid=$!
    printf '%s\n' "$pid" > "$pidfile"

    # Wait approximately 15 seconds for an actual IPC response
    local deadline=$((SECONDS + 15))

    while ((SECONDS < deadline)); do

        # Process exited before IPC initialization
        if ! kill -0 "$pid" 2>/dev/null; then
            echo "[ERROR] $name exited during startup." >&2
            echo "Check log: $logfile" >&2
            rm -f -- "$pidfile"
            return 1
        fi

        # Verify socket existence and an actual IPC response
        if [[ -S "$sock" ]] && ipc_ready "$name"; then
            echo "[OK] $name started (PID $pid), IPC responding"
            return 0
        fi

        sleep 0.2
    done

    echo "[ERROR] $name IPC did not respond during startup." >&2
    echo "Check log: $logfile" >&2
    return 1

}

# 4. Launch programs
failures=0

start_one controllerd \
    "$REPO_DIR/controller/controllerd" \
    "$REPO_DIR/controller" \
    "$MARINE_CONTROLLER_SOCKET" || ((failures+=1))

start_one sonar \
    "$REPO_DIR/sdkExample/build/bin/sdkExample" \
    "$REPO_DIR/sdkExample/build/bin" \
    "$MARINE_SONAR_SOCKET" --service || ((failures+=1))

start_one dvl \
    "$REPO_DIR/dvl_test" \
    "$REPO_DIR" \
    "$MARINE_DVL_SOCKET" --service || ((failures+=1))

# Start Servo as the logged-in user
start_one servo \
    "$REPO_DIR/servo" \
    "$REPO_DIR" \
    "$MARINE_SERVO_SOCKET" --service || ((failures+=1))

echo "[INFO] Logs: $LOG_DIR"

((failures == 0)) || exit 1
