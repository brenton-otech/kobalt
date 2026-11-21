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
# The launcher refuses to start Kobalt while this folder names a living helper,
# so a tap on Kobalt during the update cannot start a half-installed build.
LOCK=${KOBALT_UPDATE_LOCK:-/var/tmp/kobalt-update/active}
# The exit can include a final save and the stock UI's return. Ten ticks a second.
WAIT_TICKS=${KOBALT_UPDATE_WAIT_TICKS:-1800}
LOG=$APP_DIR/logs/update.log
cd / || exit 1
mkdir -p "$APP_DIR/logs" || exit 1

say()
{
    echo "$(date -u '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null) $*"
}

# One line on the stock screen, which shows while the update runs. Best effort:
# the stock interface can draw over it.
show()
{
    command -v eips >/dev/null 2>&1 || return 0
    eips 2 2 "$1" >/dev/null 2>&1 || :
}

unlock()
{
    rm -f "$LOCK/pid"
    rmdir "$LOCK" 2>/dev/null || :
}

mkdir -p "$LOCK" || exit 1
echo "$$" >"$LOCK/pid"
trap unlock EXIT
trap 'exit 143' HUP INT TERM

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
    show 'Updating Kobalt. It starts again when the update is done.'
    if [ ! -x "$KPM" ]; then
        say "KPM is missing at $KPM; the update was not attempted."
    else
        # install takes the newest kobalt alone; upgrade would move every package.
        "$KPM" -y install kobalt
        say "kpm install kobalt exited with status $?."
    fi
    say 'Starting Kobalt.'
    show 'Starting Kobalt...                                         '
    # The launcher takes over from here; it must not find this helper's lock.
    unlock
    sh "$APP_DIR/launcher/launch.sh"
    say "Launcher exited with status $?."
} >>"$LOG" 2>&1
