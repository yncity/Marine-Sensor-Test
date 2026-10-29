#!/bin/sh
set -eu

# Marine-Sensor-Test PWM permissions
# Raspberry Pi 3B+ / GPIO18 / PWM0
#
# udev add:
#   Grant gpio group permission to export/unexport.
#
# udev change:
#   Grant gpio group permission to PWM0 attributes.
#
# Do not export PWM inside the udev worker.
# Do not enable PWM output here.

CHIP="/sys/class/pwm/pwmchip0"

[ -d "$CHIP" ] || exit 0

# 1. Allow the gpio group to export and unexport PWM channels.
for attr in export unexport; do

    file="$CHIP/$attr"

    [ -e "$file" ] || {
        echo "[ERROR] Missing PWM attribute: $file" >&2
        exit 1
    }

    chgrp gpio "$file"
    chmod g+w "$file"
done

# 2. pwm0 might not exist during the initial add event.
# It will be created by marine-start.sh.
if [ ! -d "$CHIP/pwm0" ]; then
    exit 0
fi

# 3. Set permissions after PWM0 has been exported.
for attr in period duty_cycle enable; do

    file="$CHIP/pwm0/$attr"

    [ -e "$file" ] || {
        echo "[ERROR] Missing PWM attribute: $file" >&2
        exit 1
    }

    chgrp gpio "$file"
    chmod 0660 "$file"
done

exit 0
