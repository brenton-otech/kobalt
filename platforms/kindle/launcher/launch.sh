#!/bin/sh

# KUAL invokes this wrapper. The supervisor survives both its caller and an app crash.
set -eu
# Kobalt pauses the stock interface while it runs, as KOReader does, so Home
# returns almost at once. --pause-ui is accepted from older menus; the former
# stop mode, which shut the framework down and cold-booted it, is retired.
case "${1:-}" in
    --pause-ui) shift ;;
    --stop-ui)
        echo 'Kobalt no longer stops the stock UI. Start it without --stop-ui.' >&2
        exit 1
        ;;
esac
APP_DIR=$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)
# An update in progress owns the installation: KPM is replacing these files.
# A lock whose helper has died is stale and is removed.
UPDATE_LOCK=${KOBALT_UPDATE_LOCK:-/var/tmp/kobalt-update/active}
if [ -d "$UPDATE_LOCK" ]; then
    update_pid=$(cat "$UPDATE_LOCK/pid" 2>/dev/null) || update_pid=
    case "$update_pid" in ''|*[!0-9]*) update_pid= ;; esac
    if [ -n "$update_pid" ] && kill -0 "$update_pid" 2>/dev/null; then
        echo 'Kobalt is updating. It starts again when the update is done.' >&2
        ! command -v eips >/dev/null 2>&1 || eips 2 2 'Kobalt is updating. It starts again when done.' >/dev/null 2>&1 || :
        exit 1
    fi
    rm -f "$UPDATE_LOCK/pid"
    rmdir "$UPDATE_LOCK" 2>/dev/null || :
fi
if [ "$(uname -s)" != Linux ]; then
    echo 'Kobalt Kindle requires Kindle Linux.' >&2
    exit 1
fi
sh "$APP_DIR/launcher/check-runtime.sh" "$APP_DIR" || exit 1
if [ -e /var/local/system/userpasswdenabled ]; then
    echo 'Kobalt prototype cannot run with the Kindle passcode enabled.' >&2
    exit 1
fi
[ -x "$APP_DIR/bin/kobalt" ] || { echo 'Kobalt executable is missing.' >&2; exit 1; }
[ -x "$APP_DIR/bin/kobalt-power-bridge" ] || { echo 'Kindle power bridge is missing.' >&2; exit 1; }
[ -f "$APP_DIR/launcher/supervisor.sh" ] || exit 1
sh "$APP_DIR/launcher/check-storage.sh" "$APP_DIR" || exit 1
required_tools='status nohup usleep readlink ps grep umount lipc-get-prop lipc-set-prop pidof cat sed head date'
for tool in $required_tools; do
    command -v "$tool" >/dev/null || { echo "Required Kindle command missing: $tool" >&2; exit 1; }
done

# A stale lock is kept after an uncatchable supervisor failure. Do not start a
# second reader over a UI whose ownership needs to be checked first.
RUN_DIR=/var/tmp/kobalt-kindle-prototype
mkdir -p "$APP_DIR/logs"
if ! mkdir "$RUN_DIR" 2>/dev/null; then
    echo "Kobalt is running, or its previous supervisor failed. Inspect $RUN_DIR before removing it." >&2
    exit 1
fi
trap 'rm -f "$RUN_DIR/supervisor.sh" "$RUN_DIR/power-bridge"; rmdir "$RUN_DIR" 2>/dev/null || :' EXIT
chmod 700 "$RUN_DIR"
if ! cp "$APP_DIR/launcher/supervisor.sh" "$RUN_DIR/supervisor.sh"; then
    rmdir "$RUN_DIR"
    exit 1
fi
cp "$APP_DIR/bin/kobalt-power-bridge" "$RUN_DIR/power-bridge"
chmod 700 "$RUN_DIR/power-bridge"
# Keep the supervisor and its open files on system storage. USB export must
# not depend on a shell or log descriptor inside /mnt/us.
cd /
nohup sh "$RUN_DIR/supervisor.sh" "$APP_DIR" "$RUN_DIR" "$@" >>"$RUN_DIR/launcher.log" 2>&1 </dev/null &
trap - EXIT
echo "Kobalt supervisor started: $!"
