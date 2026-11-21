#!/bin/sh

# Updates Kobalt from the KPM repositories and starts it again. Kobalt copies
# this helper to system storage and starts it before it exits, because KPM
# replaces the files under the app folder and refuses to run while the reader
# holds its run folder. Whatever KPM does, the helper ends by starting the
# Kobalt that is installed then, so a failed update returns to the old build.
set -u
APP_DIR=$1
RUN_DIR=$2
KPM=${KOBALT_KPM:-/var/local/kmc/bin/kpm}
# The exit can include a final save and the stock UI's return. Ten ticks a second.
WAIT_TICKS=${KOBALT_UPDATE_WAIT_TICKS:-1800}
LOG=$APP_DIR/logs/update.log
cd / || exit 1
mkdir -p "$APP_DIR/logs" || exit 1

say()
{
    echo "$(date -u '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null) $*"
}

{
    say 'Update requested; waiting for Kobalt to exit.'
    while [ -d "$RUN_DIR" ] && [ "$WAIT_TICKS" -gt 0 ]; do
        usleep 100000 || sleep 1
        WAIT_TICKS=$((WAIT_TICKS - 1))
    done
    if [ -d "$RUN_DIR" ]; then
        # Kobalt still owns the screen. Starting or replacing it now would fail.
        say 'Kobalt did not exit; the update was not attempted.'
        exit 1
    fi
    if [ ! -x "$KPM" ]; then
        say "KPM is missing at $KPM; the update was not attempted."
    else
        # install takes the newest kobalt alone; upgrade would move every package.
        "$KPM" -y install kobalt
        say "kpm install kobalt exited with status $?."
    fi
    say 'Starting Kobalt.'
    sh "$APP_DIR/launcher/launch.sh"
    say "Launcher exited with status $?."
} >>"$LOG" 2>&1
