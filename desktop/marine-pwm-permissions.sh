
#!/bin/sh
set -eu

# Allow the gpio group to access PWM0 through sysfs.
# This script runs as root from a udev rule.
# It never enables PWM output.

CHIP="/sys/class/pwm/pwmchip0"

[ -d "$CHIP" ] || exit 0

# Export PWM channel 0 only if it is not already exported.
# Exporting the channel does not enable its output.
if [ ! -d "$CHIP/pwm0" ]; then
    if ! printf '0' > "$CHIP/export"; then
        [ -d "$CHIP/pwm0" ] || exit 1
    fi
fi

# Wait briefly for the PWM channel files.
count=0

while [ ! -d "$CHIP/pwm0" ] && [ "$count" -lt 20 ]; do
    sleep 0.05
    count=$((count + 1))
done

[ -d "$CHIP/pwm0" ] || exit 1

# Grant access only to root and the gpio group.
for attr in \
    export \
    unexport \
    pwm0/period \
    pwm0/duty_cycle \
    pwm0/enable; do

    chgrp gpio "$CHIP/$attr"
    chmod 0660 "$CHIP/$attr"
done

exit 0
