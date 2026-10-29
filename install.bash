
#!/usr/bin/env bash

set -Eeuo pipefail

# ============================================================
# Marine-Sensor-Test Installation
#
# Target: Raspberry Pi OS (Desktop)
# Execution: Normal user with sudo permissions
#
# Current implementation:
#   STEP 1 - System environment check
# ============================================================


# ------------------------------------------------------------
# 0. Global variables
# ------------------------------------------------------------

CURRENT_STEP=0

# Resolve repository root from this script's location
REPO_DIR="$(
    cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P
)"


# ------------------------------------------------------------
# 1. Common functions
# ------------------------------------------------------------

error_exit() {
    echo
    echo "[ERROR] STEP ${CURRENT_STEP}: $1" >&2
    exit 1
}

on_error() {
    local exit_code="$1"
    local line="$2"
    local command="$3"

    echo
    echo "[ERROR] Installation failed." >&2
    echo "  STEP : $CURRENT_STEP" >&2
    echo "  LINE : $line" >&2
    echo "  CMD  : $command" >&2
    echo "  CODE : $exit_code" >&2

    exit "$exit_code"
}

trap 'on_error "$?" "$LINENO" "$BASH_COMMAND"' ERR


# Require confirmation before every STEP
confirm_step() {
    local step="$1"
    local title="$2"
    local answer=""

    CURRENT_STEP="$step"

    echo
    echo "========================================"
    echo " STEP $step/7 - $title"
    echo "========================================"

    if ! read -r -p "Continue? [y/N]: " answer; then
        echo "[CANCELLED] No input received."
        exit 2
    fi

    if [[ "$answer" != "y" ]]; then
        echo "[CANCELLED] Installation stopped by user."
        exit 2
    fi

    echo "[INFO] Starting STEP $step..."
}


# ------------------------------------------------------------
# 2. STEP 1 - System environment check
# ------------------------------------------------------------

step_1() {

    echo
    echo "[1-1] Checking operating system..."

    [[ "$(uname -s)" == "Linux" ]] ||
        error_exit "Linux is required."

    [[ -r /etc/os-release ]] ||
        error_exit "/etc/os-release not found."

    source /etc/os-release

    case "${ID:-}" in
        raspbian|debian)
            echo "[OK] OS: ${PRETTY_NAME:-Unknown}"
            ;;
        *)
            error_exit "Raspberry Pi OS is required. Detected: ${ID:-unknown}"
            ;;
    esac


    echo
    echo "[1-2] Checking Raspberry Pi hardware..."

    [[ -r /proc/device-tree/model ]] ||
        error_exit "Raspberry Pi hardware not detected."

    local model
    model="$(tr -d '\000' < /proc/device-tree/model)"

    [[ "$model" == *"Raspberry Pi"* ]] ||
        error_exit "Unsupported hardware: $model"

    echo "[OK] Hardware: $model"


    echo
    echo "[1-3] Checking architecture..."

    local arch bits

    arch="$(uname -m)"
    bits="$(getconf LONG_BIT)"

    case "$arch" in
        aarch64|armv7l|armv6l)
            echo "[OK] CPU architecture: $arch"
            ;;
        *)
            error_exit "Unsupported CPU architecture: $arch"
            ;;
    esac

    case "$bits" in
        32|64)
            echo "[OK] OS bitness: ${bits}-bit"
            ;;
        *)
            error_exit "Unsupported OS bitness: $bits"
            ;;
    esac


    echo
    echo "[1-4] Checking installation user..."

    [[ "$(id -u)" -ne 0 ]] ||
        error_exit "Do not run this installer as root."

    [[ -n "${HOME:-}" && -d "$HOME" ]] ||
        error_exit "A valid user HOME directory is required."

    echo "[OK] User: $(id -un)"
    echo "[OK] HOME: $HOME"


    echo
    echo "[1-5] Checking sudo permissions..."

    command -v sudo >/dev/null 2>&1 ||
        error_exit "sudo is not installed."

    if ! sudo -v; then
        error_exit "sudo authentication failed."
    fi

    echo "[OK] sudo permission verified."


    echo
    echo "[1-6] Checking package manager..."

    command -v apt-get >/dev/null 2>&1 ||
        error_exit "apt-get not found."

    command -v dpkg-query >/dev/null 2>&1 ||
        error_exit "dpkg-query not found."

    echo "[OK] APT package manager available."


    echo
    echo "[1-7] Checking repository files..."

    echo "[INFO] Repository: $REPO_DIR"

    # Desktop Exec paths require a simple installation path
    if [[ "$REPO_DIR" =~ [[:space:]] ]]; then
        error_exit "Repository path must not contain whitespace."
    fi

    local required_files=(
        "sdkExample/CMakeLists.txt"
        "sdkExample/islSdk/CMakeLists.txt"
        "sdkExample/src/main.cpp"
        "sdkExample/src/sonarApp.cpp"

        "dvl_test.c"
        "servo.c"

        "controller/controller.c"
        "controller/controllerd.c"
        "controller/controller_tui.c"

        "desktop/marine-start.sh"
        "desktop/marine-stop.sh"
        "desktop/marine-launch.sh"
        "desktop/marine-ipc.env"
        "desktop/marine-autostart.desktop.in"
        "desktop/marine-launch.desktop.in"
    )

    local file

    for file in "${required_files[@]}"; do
        [[ -f "$REPO_DIR/$file" ]] ||
            error_exit "Missing required file: $file"
    done

    echo "[OK] All required source files found."


    echo
    echo "[1-8] Checking desktop environment..."

    if compgen -G '/usr/share/xsessions/*.desktop' > /dev/null ||
       compgen -G '/usr/share/wayland-sessions/*.desktop' > /dev/null; then
        echo "[OK] Graphical desktop session detected."
    else
        error_exit "A graphical desktop environment is required."
    fi

    if [[ -z "${XDG_RUNTIME_DIR:-}" ]]; then
        echo "[INFO] No active desktop runtime detected."
        echo "[INFO] Installation may run over SSH."
        echo "[INFO] Runtime will be created by the login session."
    fi


    echo
    echo "[1-9] Checking available storage..."

    df -h -- "$REPO_DIR"

    echo
    echo "[OK] STEP 1 completed successfully."
}


# ------------------------------------------------------------
# 3. STEP 2 - Install required packages
# ------------------------------------------------------------

step_2() {

    echo
    echo "[2-1] Updating APT package lists..."

    # Stop if any repository update fails
    sudo apt-get -o APT::Update::Error-Mode=any update

    echo "[OK] APT package lists updated."


    echo
    echo "[2-2] Installing required packages..."

    local packages=(
        build-essential
        cmake
        pkg-config

        libopencv-dev
        libncurses-dev

        git
        util-linux
        coreutils

        desktop-file-utils
        xdg-user-dirs
    )

    printf '  - %s\n' "${packages[@]}"

    # Use APT to install missing packages and dependencies.
    # Do not perform a full OS upgrade.
    sudo env DEBIAN_FRONTEND=noninteractive \
        apt-get install -y --no-install-recommends \
        "${packages[@]}"

    echo "[OK] Package installation completed."


    echo
    echo "[2-3] Checking required commands..."

    local required_commands=(
        gcc
        g++
        make
        cmake
        pkg-config

        git
        flock
        timeout
        tail
        readlink
        nohup

        xdg-user-dir
        desktop-file-validate
    )

    local cmd

    for cmd in "${required_commands[@]}"; do

        if ! command -v "$cmd" >/dev/null 2>&1; then
            error_exit "Required command not found: $cmd"
        fi

        echo "[OK] $cmd"
    done


    echo
    echo "[2-4] Checking OpenCV..."

    if ! pkg-config --exists opencv4; then
        error_exit "OpenCV development files not detected."
    fi

    echo "[OK] OpenCV version: $(pkg-config --modversion opencv4)"


    echo
    echo "[2-5] Checking ncurses..."

    if ! pkg-config --exists ncursesw; then
        error_exit "ncurses development files not detected."
    fi

    echo "[OK] ncurses version: $(pkg-config --modversion ncursesw)"


    echo
    echo "[2-6] Checking compiler and CMake..."

    gcc --version | head -n 1
    g++ --version | head -n 1
    cmake --version | head -n 1

    echo
    echo "[OK] STEP 2 completed successfully."
}


# ------------------------------------------------------------
# 4. STEP 3 - Build all programs
# ------------------------------------------------------------

# Execute a build command and save its output.
# Any failure stops the installation immediately.
run_build() {
    local name="$1"
    shift

    local logfile="$BUILD_LOG_DIR/${name}.log"
    local result=0

    echo
    echo "----------------------------------------"
    echo "[BUILD] $name"
    echo "[LOG]   $logfile"
    echo "----------------------------------------"

    "$@" 2>&1 | tee "$logfile" || result=$?

    if ((result != 0)); then
        echo "[ERROR] Build failed: $name" >&2
        echo "[ERROR] Exit code: $result" >&2
        echo "[ERROR] See log: $logfile" >&2
        error_exit "STEP 3 failed while building $name."
    fi

    echo "[OK] $name completed."
}


# Verify a generated executable.
check_binary() {
    local binary="$1"

    if [[ ! -f "$binary" || ! -x "$binary" ]]; then
        error_exit "Build output missing or not executable: $binary"
    fi

    echo "[OK] $(realpath --relative-to="$REPO_DIR" "$binary")"
}


step_3() {

    echo
    echo "[3-1] Preparing build configuration..."

    local build_dir="$REPO_DIR/sdkExample/build"
    local bits
    local x32_bit="OFF"
    local build_jobs="${MARINE_BUILD_JOBS:-1}"

    BUILD_LOG_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/marine-sensors/install-logs"

    mkdir -p -- "$BUILD_LOG_DIR"

    # Match CMake SDK configuration to the OS bitness.
    bits="$(getconf LONG_BIT)"

    case "$bits" in
        32)
            x32_bit="ON"
            ;;
        64)
            x32_bit="OFF"
            ;;
        *)
            error_exit "Unsupported OS bitness: $bits"
            ;;
    esac

    if [[ ! "$build_jobs" =~ ^[1-9][0-9]*$ ]]; then
        error_exit "MARINE_BUILD_JOBS must be a positive integer."
    fi

    echo "[INFO] Repository: $REPO_DIR"
    echo "[INFO] OS bitness: $bits"
    echo "[INFO] CMake X32_BIT: $x32_bit"
    echo "[INFO] Build jobs: $build_jobs"
    echo "[INFO] Build logs: $BUILD_LOG_DIR"


    # --------------------------------------------------------
    # 3-2. Configure Sonar SDK and applications
    # --------------------------------------------------------

    echo
    echo "[3-2] Configuring Sonar CMake project..."

    run_build "sonar-configure" \
        cmake \
        -S "$REPO_DIR/sdkExample" \
        -B "$build_dir" \
        -DCMAKE_BUILD_TYPE=Release \
        -DX32_BIT="$x32_bit"


    # --------------------------------------------------------
    # 3-3. Build Sonar programs
    # --------------------------------------------------------

    echo
    echo "[3-3] Building Sonar SDK and programs..."

    run_build "sonar-build" \
        cmake --build "$build_dir" \
        --parallel "$build_jobs"

    check_binary "$build_dir/bin/sdkExample"
    check_binary "$build_dir/bin/sonarReplay"


    # --------------------------------------------------------
    # 3-4. Build DVL
    # --------------------------------------------------------

    echo
    echo "[3-4] Building DVL..."

    run_build "dvl-build" \
        gcc -std=c11 -O2 -Wall -Wextra -pthread \
        "$REPO_DIR/dvl_test.c" \
        -o "$REPO_DIR/dvl_test" \
        -lm

    check_binary "$REPO_DIR/dvl_test"


    # --------------------------------------------------------
    # 3-5. Build Servo
    # --------------------------------------------------------

    echo
    echo "[3-5] Building Servo..."

    run_build "servo-build" \
        gcc -std=c11 -O2 -Wall -Wextra -pthread \
        "$REPO_DIR/servo.c" \
        -o "$REPO_DIR/servo"

    check_binary "$REPO_DIR/servo"


    # --------------------------------------------------------
    # 3-6. Build Controller CLI
    # --------------------------------------------------------

    echo
    echo "[3-6] Building Controller CLI..."

    run_build "controller-cli-build" \
        gcc -std=c11 -O2 -Wall -Wextra \
        "$REPO_DIR/controller/controller.c" \
        -o "$REPO_DIR/controller/controller"

    check_binary "$REPO_DIR/controller/controller"


    # --------------------------------------------------------
    # 3-7. Build Controller daemon
    # --------------------------------------------------------

    echo
    echo "[3-7] Building Controller daemon..."

    run_build "controllerd-build" \
        gcc -std=c11 -O2 -Wall -Wextra -pthread \
        "$REPO_DIR/controller/controllerd.c" \
        -o "$REPO_DIR/controller/controllerd"

    check_binary "$REPO_DIR/controller/controllerd"


    # --------------------------------------------------------
    # 3-8. Build Controller TUI
    # --------------------------------------------------------

    echo
    echo "[3-8] Building Controller TUI..."

    local -a ncurses_flags=()

    read -r -a ncurses_flags <<< \
        "$(pkg-config --cflags --libs ncursesw)"

    run_build "controller-tui-build" \
        gcc -std=c11 -O2 -Wall -Wextra -pthread \
        "$REPO_DIR/controller/controller_tui.c" \
        -o "$REPO_DIR/controller/controller_tui" \
        "${ncurses_flags[@]}"

    check_binary "$REPO_DIR/controller/controller_tui"


    # --------------------------------------------------------
    # 3-9. Final build verification
    # --------------------------------------------------------

    echo
    echo "[3-9] Verifying all build outputs..."

    local binaries=(
        "$build_dir/bin/sdkExample"
        "$build_dir/bin/sonarReplay"
        "$REPO_DIR/dvl_test"
        "$REPO_DIR/servo"
        "$REPO_DIR/controller/controller"
        "$REPO_DIR/controller/controllerd"
        "$REPO_DIR/controller/controller_tui"
    )

    local binary

    for binary in "${binaries[@]}"; do
        check_binary "$binary"
    done

    echo
    echo "[OK] All 7 programs built successfully."
    echo "[OK] STEP 3 completed successfully."
}


# ------------------------------------------------------------
# 5. STEP 4 - Configure user runtime and storage
# ------------------------------------------------------------

step_4() {

    echo
    echo "[4-1] Validating IPC configuration..."

    local env_file="$REPO_DIR/desktop/marine-ipc.env"
    local session_runtime="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
    local state_base="${XDG_STATE_HOME:-$HOME/.local/state}"
    local log_dir="$state_base/marine-sensors/logs"
    local install_log_dir="$state_base/marine-sensors/install-logs"
    local runtime_expected="$session_runtime/marine-sensors"
    local saved_umask
    local -a sockets expected_sockets
    local i file dir src

    [[ "$session_runtime" == /* ]] ||
        error_exit "XDG_RUNTIME_DIR must be absolute."

    [[ "$state_base" == /* ]] ||
        error_exit "XDG_STATE_HOME must be absolute."

    [[ -r "$env_file" ]] ||
        error_exit "IPC environment file not found."

    bash -n "$env_file" ||
        error_exit "IPC environment file has a syntax error."

    # Use a local runtime value for configuration validation.
    # No actual runtime files are created here.
    local XDG_RUNTIME_DIR="$session_runtime"
    local MARINE_IPC_DIR=""
    local MARINE_SONAR_SOCKET=""
    local MARINE_DVL_SOCKET=""
    local MARINE_SERVO_SOCKET=""
    local MARINE_CONTROLLER_SOCKET=""

    source "$env_file"

    [[ "$MARINE_IPC_DIR" == "$runtime_expected" ]] ||
        error_exit "MARINE_IPC_DIR does not match user runtime directory."

    sockets=(
        "$MARINE_SONAR_SOCKET"
        "$MARINE_DVL_SOCKET"
        "$MARINE_SERVO_SOCKET"
        "$MARINE_CONTROLLER_SOCKET"
    )

    expected_sockets=(
        "$runtime_expected/sonar.sock"
        "$runtime_expected/dvl.sock"
        "$runtime_expected/servo.sock"
        "$runtime_expected/controller.sock"
    )

    for ((i=0; i<${#sockets[@]}; i++)); do

        [[ "${sockets[i]}" == "${expected_sockets[i]}" ]] ||
            error_exit "IPC socket path mismatch: ${sockets[i]}"

        ((${#sockets[i]} < 108)) ||
            error_exit "IPC socket path is too long: ${sockets[i]}"
    done

    echo "[OK] IPC runtime: $runtime_expected"
    printf '[OK] Socket: %s\n' "${sockets[@]}"


    # --------------------------------------------------------
    # 4-2. Check runtime environment
    # --------------------------------------------------------

    echo
    echo "[4-2] Checking logged-in user runtime directory..."

    if [[ -d "$session_runtime" ]]; then

        [[ -O "$session_runtime" && -w "$session_runtime" ]] ||
            error_exit "User runtime directory is not owned/writable by this user."

        echo "[OK] Runtime base is owned by current user: $session_runtime"

    else
        echo "[INFO] Runtime base does not exist in this session."
        echo "[INFO] It will be prepared by the user login session."
    fi

    echo "[INFO] IPC sockets and PID files will be created only at program start."


    # --------------------------------------------------------
    # 4-3. Prepare persistent directories
    # --------------------------------------------------------

    echo
    echo "[4-3] Preparing persistent data directories..."

    saved_umask="$(umask)"
    umask 077

    for dir in \
        "$log_dir" \
        "$install_log_dir" \
        "$REPO_DIR/Video" \
        "$REPO_DIR/Logfile"; do

        mkdir -p -- "$dir"

        [[ -d "$dir" && -O "$dir" && -w "$dir" ]] ||
            error_exit "Directory is not owned/writable: $dir"
    done

    chmod 700 -- "$log_dir" "$install_log_dir"

    echo "[OK] Device logs: $log_dir"
    echo "[OK] Installer logs: $install_log_dir"
    echo "[OK] Sonar video: $REPO_DIR/Video"
    echo "[OK] Sonar ISLOG: $REPO_DIR/Logfile"


    # --------------------------------------------------------
    # 4-4. Prepare device log files
    # --------------------------------------------------------

    echo
    echo "[4-4] Preparing device log files..."

    for file in sonar.log dvl.log servo.log controllerd.log; do

        [[ ! -L "$log_dir/$file" ]] ||
            error_exit "Log file is a symbolic link: $log_dir/$file"

        : >> "$log_dir/$file"

        [[ -f "$log_dir/$file" && -O "$log_dir/$file" ]] ||
            error_exit "Log file is not owned by current user: $file"

        chmod 600 -- "$log_dir/$file"

        echo "[OK] $file"
    done


    # --------------------------------------------------------
    # 4-5. Check execution scripts
    # --------------------------------------------------------

    echo
    echo "[4-5] Checking script syntax, paths and execution permissions..."

    for file in marine-start.sh marine-stop.sh marine-launch.sh; do

        src="$REPO_DIR/desktop/$file"

        bash -n "$src" ||
            error_exit "Script syntax check failed: $file"

        grep -Fq 'RUN_DIR="$MARINE_IPC_DIR"' "$src" ||
            error_exit "Script does not use common runtime directory: $file"

        chmod u+x -- "$src"

        [[ -x "$src" ]] ||
            error_exit "Script is not executable: $file"

        echo "[OK] $file"
    done

    chmod u+x -- "$REPO_DIR/install.bash"


    # --------------------------------------------------------
    # 4-6. Verify IPC socket permission implementation
    # --------------------------------------------------------

    echo
    echo "[4-6] Checking IPC server socket permissions..."

    for src in \
        "$REPO_DIR/sdkExample/src/sonarCommandSocket.h" \
        "$REPO_DIR/dvlCommandSocket.h" \
        "$REPO_DIR/servoCommandSocket.h" \
        "$REPO_DIR/controller/controllerd.c"; do

        [[ -f "$src" ]] ||
            error_exit "IPC source file missing: $src"

        grep -Eq 'chmod\([^,]+,[[:space:]]*0600\)' "$src" ||
            error_exit "Expected owner-only socket permission (0600): $src"

        echo "[OK] ${src#"$REPO_DIR/"}"
    done

    umask "$saved_umask"

    echo
    echo "[OK] STEP 4 completed successfully."
}

# ------------------------------------------------------------
# Main
# ------------------------------------------------------------

main() {

    echo "========================================"
    echo " Marine Sensor Installation"
    echo "========================================"

    echo "Repository: $REPO_DIR"
    echo "Target    : Raspberry Pi OS Desktop"
    echo "Mode      : User-level desktop autostart"

    [[ -t 0 ]] ||
        error_exit "Run this installer in an interactive terminal."

    # STEP 1 - Environment
    confirm_step 1 "System Environment Check"
    step_1

    # STEP 2 - Dependencies
    confirm_step 2 "Install Required Packages"
    step_2

    # STEP 3 - Build all programs
    confirm_step 3 "Build Sonar, DVL, Servo and Controller"
    step_3

    # STEP 4 - User runtime and storage configuration
    confirm_step 4 "Configure IPC Runtime and Storage"
    step_4

    echo " STEP 1-4 COMPLETED"
    echo "========================================"

    echo "[INFO] STEP 5-7 are not implemented yet."
    echo "[INFO] Installation is NOT complete."
}

main "$@"