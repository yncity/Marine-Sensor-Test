#!/usr/bin/env bash
set -Eeuo pipefail

# Marine-Sensor-Test user-level uninstaller
# Run as the installing user: bash uninstall.bash (never sudo bash).
# Keeps: OS/APT packages, network IP configuration, gpio group, repository.

REPO_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
RUNTIME="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
RUN_DIR="$RUNTIME/marine-sensors"
STATE_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/marine-sensors"
AUTOSTART="${XDG_CONFIG_HOME:-$HOME/.config}/autostart/marine-autostart.desktop"
BOOT_CFG=/boot/firmware/config.txt
[[ -f "$BOOT_CFG" ]] || BOOT_CFG=/boot/config.txt
BACKUP=""
EXPECTED=""

fail() { echo "[ERROR] $*" >&2; exit 1; }
cleanup() { [[ -z "$EXPECTED" ]] || rm -f -- "$EXPECTED"; }
trap cleanup EXIT

[[ "$(id -u)" -ne 0 ]] || fail "Run as the normal user, not root."
[[ -t 0 ]] || fail "Run interactively in a terminal."
[[ -f "$REPO_DIR/desktop/marine-stop.sh" ]] || fail "Run from the project repository."
[[ -f "$BOOT_CFG" ]] || fail "Raspberry Pi config.txt not found."
[[ "$RUNTIME" == "/run/user/$(id -u)" ]] || fail "Unexpected XDG_RUNTIME_DIR: $RUNTIME"
[[ "$STATE_DIR" == "$HOME/"* ]] || fail "State directory must be inside HOME."
[[ "$AUTOSTART" == "$HOME/"* ]] || fail "Autostart directory must be inside HOME."
[[ ! -L "$STATE_DIR" ]] || fail "State directory is a symbolic link."
command -v xdg-user-dir >/dev/null || fail "xdg-user-dir is missing."

DESKTOP_DIR="$(xdg-user-dir DESKTOP)"
[[ "$DESKTOP_DIR" == "$HOME/"* ]] || fail "Desktop directory must be inside HOME: $DESKTOP_DIR"
LAUNCHER="$DESKTOP_DIR/Marine Sensor Controller.desktop"
if [[ -f "$AUTOSTART" ]]; then
    grep -Fxq "Exec=$REPO_DIR/desktop/marine-start.sh" "$AUTOSTART" ||
        fail "Autostart belongs to another repository. Refusing to delete."
fi
if [[ -f "$LAUNCHER" ]]; then
    grep -Fxq "Exec=$REPO_DIR/desktop/marine-launch.sh" "$LAUNCHER" ||
        fail "Desktop launcher belongs to another repository. Refusing to delete."
fi

# Verify boot configuration can be restored without overwriting other edits.
shopt -s nullglob
backups=("${BOOT_CFG}".marine-backup-*)
shopt -u nullglob
((${#backups[@]} > 0)) || fail "No Marine boot backup found. Restore config.txt manually before uninstalling."
BACKUP="${backups[0]}"  # Timestamped filenames sort oldest first.
[[ -f "$BACKUP" && ! -L "$BACKUP" ]] || fail "Invalid boot backup file."
EXPECTED="$(mktemp)"
sudo -v || fail "sudo authentication failed."
sudo cat -- "$BACKUP" > "$EXPECTED"
if ! grep -Eq '^[[:space:]]*dtoverlay[[:space:]]*=[[:space:]]*pwm(-2chan)?([,[:space:]]|$)' "$EXPECTED"; then
    printf '\n# Marine-Sensor-Test Servo PWM0\n[all]\ndtoverlay=pwm,pin=18,func=2\n' >> "$EXPECTED"
fi
sed -i 's/^[[:space:]]*dtparam=audio=on[[:space:]]*$/#dtparam=audio=on/' "$EXPECTED"
if ! sudo cmp -s -- "$EXPECTED" "$BOOT_CFG" && ! sudo cmp -s -- "$BACKUP" "$BOOT_CFG"; then
    echo "[ERROR] config.txt has additional changes; refusing to overwrite them." >&2
    echo "[INFO] Inspect: sudo diff -u '$BACKUP' '$BOOT_CFG'" >&2
    exit 1
fi

echo "Repository  : $REPO_DIR"
echo "Boot backup : $BACKUP"
echo "Will remove : desktop launcher, autostart, PWM helper/rule, Marine logs"
echo "Will restore: boot config from verified backup"
echo "Will keep   : Git repository, sensor recordings, APT packages, network IP"
read -r -p "Type REMOVE to uninstall: " answer
[[ "$answer" == REMOVE ]] || { echo "[CANCELLED]"; exit 2; }

# Close the TUI first, so it cannot restart devices during removal.
if pgrep -u "$(id -u)" -x controller_tui >/dev/null; then
    fail "Controller TUI is open. Close it with F10/Ctrl+X, then retry."
fi

# Gracefully stop sensor processes via the existing checked-PID stop script.
if [[ -d "$RUN_DIR" ]]; then
    echo "[1/5] Stopping Sonar, DVL, Servo..."
    XDG_RUNTIME_DIR="$RUNTIME" bash "$REPO_DIR/desktop/marine-stop.sh" ||
        fail "Device stop incomplete. Inspect PID/socket files; do not delete them blindly."
fi

# Stop controllerd separately: marine-stop.sh intentionally keeps it alive.
PID_FILE="$RUN_DIR/controllerd.pid"
if [[ -f "$PID_FILE" ]]; then
    read -r pid < "$PID_FILE" || true
    [[ "$pid" =~ ^[0-9]+$ ]] || fail "Invalid controllerd PID file."
    if kill -0 "$pid" 2>/dev/null; then
        actual="$(readlink -f -- "/proc/$pid/exe" 2>/dev/null || true)"
        expected="$(readlink -f -- "$REPO_DIR/controller/controllerd")"
        [[ "$actual" == "$expected" ]] || fail "Controller PID belongs to another program."
        echo "[2/5] Stopping controllerd (PID $pid)..."
        kill -TERM "$pid"
        for ((i=0; i<50; i++)); do
            state="$(ps -o stat= -p "$pid" 2>/dev/null || true)"
            [[ -z "$state" || "$state" == Z* ]] && break
            sleep 0.1
        done
        state="$(ps -o stat= -p "$pid" 2>/dev/null || true)"
        [[ -z "$state" || "$state" == Z* ]] || fail "controllerd did not terminate."
    fi
fi

# Never remove files while an unidentified service remains running.
for name in controllerd sdkExample dvl_test servo controller_tui; do
    while read -r found_pid; do
        [[ -n "$found_pid" ]] || continue
        state="$(ps -o stat= -p "$found_pid" 2>/dev/null || true)"
        [[ -z "$state" || "$state" == Z* ]] ||
            fail "A $name process (PID $found_pid) is still running."
    done < <(pgrep -u "$(id -u)" -x "$name" || true)
done

echo "[3/5] Removing desktop integration and persistent logs..."
rm -f -- "$AUTOSTART" "$LAUNCHER"
[[ ! -d "$STATE_DIR" ]] || rm -r -- "$STATE_DIR"

echo "[4/5] Removing installed PWM permission files..."
sudo rm -f -- /etc/udev/rules.d/99-marine-pwm.rules
sudo rm -f -- /usr/local/libexec/marine-pwm-permissions.sh
sudo udevadm control --reload-rules

echo "[5/5] Restoring verified boot configuration..."
if ! sudo cmp -s -- "$BACKUP" "$BOOT_CFG"; then
    sudo cp -a -- "$BOOT_CFG" "${BOOT_CFG}.before-marine-uninstall-$(date +%Y%m%d-%H%M%S)"
    sudo cp -a -- "$BACKUP" "$BOOT_CFG"
fi

cat <<MSG

[OK] Marine installation settings removed.
[INFO] Repository and recordings were kept: $REPO_DIR
[INFO] Boot backup files were kept for recovery.
[IMPORTANT] Reboot to release PWM settings and clear runtime sockets.

After reboot, to make a fresh clone (ONLY after saving any needed recordings):
  cd ~
  rm -rf -- '$REPO_DIR'
  git clone https://github.com/yncity/Marine-Sensor-Test.git TEST
  cd TEST
  bash install.bash
MSG
