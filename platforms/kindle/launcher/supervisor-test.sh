#!/bin/sh

# Mock only command boundaries. This exercises the real supervisor and its traps.
set -eu
SUPERVISOR=$(CDPATH='' cd "$(dirname "$0")" && pwd -P)/supervisor.sh
TEST_ROOT=${1:?pass an ignored workspace scratch directory}
mkdir -p "$TEST_ROOT"
TEST_ROOT=$(CDPATH='' cd "$TEST_ROOT" && pwd -P)
TEST_DIR=$(mktemp -d "$TEST_ROOT/supervisor-test.XXXXXX")
sleep 120 &
KINDLE_TEST_VOLUME_PID=$!
export KINDLE_TEST_VOLUME_PID
sleep 120 &
KINDLE_TEST_AWESOME_PID=$!
export KINDLE_TEST_AWESOME_PID
trap 'kill -CONT "$KINDLE_TEST_AWESOME_PID" 2>/dev/null || :; kill -TERM "$KINDLE_TEST_AWESOME_PID" 2>/dev/null || :; wait "$KINDLE_TEST_AWESOME_PID" 2>/dev/null || :; kill -CONT "$KINDLE_TEST_VOLUME_PID" 2>/dev/null || :; kill -TERM "$KINDLE_TEST_VOLUME_PID" 2>/dev/null || :; wait "$KINDLE_TEST_VOLUME_PID" 2>/dev/null || :; rm -rf "$TEST_DIR"' EXIT HUP INT TERM
mkdir -p "$TEST_DIR/tools" "$TEST_DIR/app/bin" "$TEST_DIR/app/logs" "$TEST_DIR/app/launcher"
echo colorsoft >"$TEST_DIR/app/launcher/runtime-target"
export KINDLE_TEST_ROOT="$TEST_DIR"
# Bound the wait for an active stock application in these checks.
export KOBALT_KINDLE_HOME_WAIT=40
storage_check=$(dirname "$SUPERVISOR")/check-storage.sh
printf 'root / ext4 rw 0 0\nfsp /mnt/us fuse.fsp rw,noexec 0 0\n' >"$TEST_DIR/mounts"
if sh "$storage_check" /mnt/us/kobalt "$TEST_DIR/mounts"; then
    echo 'Storage check accepted noexec.' >&2; exit 1
fi
# A similarly named sibling is not covered by /mnt/us.
sh "$storage_check" /mnt/user/kobalt "$TEST_DIR/mounts"
printf 'bind /mnt/us/kobalt ext4 rw 0 0\n' >>"$TEST_DIR/mounts"
sh "$storage_check" /mnt/us/kobalt "$TEST_DIR/mounts"
printf 'bind /mnt/us/my\\040app ext4 rw 0 0\n' >>"$TEST_DIR/mounts"
sh "$storage_check" '/mnt/us/my app' "$TEST_DIR/mounts"
: >"$TEST_DIR/mounts"
if sh "$storage_check" /mnt/us/kobalt "$TEST_DIR/mounts"; then
    echo 'Storage check accepted an unknown mount.' >&2; exit 1
fi
cat >"$TEST_DIR/tools/status" <<'EOF'
#!/bin/sh
if [ "$1" = volumd ]; then echo "volumd start/running, process $KINDLE_TEST_VOLUME_PID"; exit 0; fi
echo 'lab126_gui start/running, process 123'
EOF
cat >"$TEST_DIR/tools/stop" <<'EOF'
#!/bin/sh
echo stop >>"$KINDLE_TEST_ROOT/actions"
[ "${KINDLE_TEST_STOP_FAIL:-0}" = 0 ]
EOF
cat >"$TEST_DIR/tools/start" <<'EOF'
#!/bin/sh
echo start >>"$KINDLE_TEST_ROOT/actions"
if [ "${KINDLE_TEST_START_HANG:-0}" = 1 ]; then
    echo "$$" >"$KINDLE_TEST_ROOT/hung-command.pid"
    exec sleep 120
fi
case "${LD_LIBRARY_PATH:-}" in "$KINDLE_TEST_ROOT/app/lib"*)
    echo 'Stock recovery inherited application libraries.' >&2
    exit 97
esac
[ "${KINDLE_TEST_START_FAIL:-0}" = 0 ]
EOF
cat >"$TEST_DIR/tools/usleep" <<'EOF'
#!/bin/sh
case "$1" in
1250000) echo delay >>"$KINDLE_TEST_ROOT/delays" ;;
100000|1000000) sleep 0.001 ;;
*) exit 1 ;;
esac
EOF
cat >"$TEST_DIR/app/bin/kobalt" <<'EOF'
#!/bin/sh
case "$(ps -o stat= -p "$KINDLE_TEST_VOLUME_PID")" in *T*) ;; *) exit 99 ;; esac
[ "$(pwd)" = / ] || exit 98
[ "${KOBALT_KINDLE_FRAMEWORK_RUNNING:-}" = 0 ] || { echo 'Stopped framework reported as running.' >&2; exit 89; }
case "${LD_LIBRARY_PATH:-}" in "$KINDLE_TEST_ROOT/app/lib"*) ;; *) exit 96 ;; esac
if grep -Eq '(^| )(splash|screensaver)( |$)' "$KINDLE_TEST_ROOT/modules"; then
    echo 'Reader started before stock overlays were released.' >&2
    exit 95
fi
echo app >>"$KINDLE_TEST_ROOT/actions"
mkdir "$KOBALT_KINDLE_RUN_DIR/waveform-abandoned"
exit "${KINDLE_TEST_APP_EXIT:-0}"
EOF
chmod +x "$TEST_DIR/tools/status" "$TEST_DIR/tools/start" "$TEST_DIR/tools/stop" "$TEST_DIR/tools/usleep" "$TEST_DIR/app/bin/kobalt"
cat >"$TEST_DIR/tools/lipc-get-prop" <<'EOF'
#!/bin/sh
set -eu
# Colorsoft's framework flag may stay zero until Home is activated.
if [ "$4" = frameworkStarted ]; then echo 0; exit 0; fi
# A restarted Colorsoft framework shows no application until Home is
# requested; other targets open Home themselves.
if [ "$4" = activeApp ]; then
    [ "${KINDLE_TEST_NO_ACTIVE_APP:-0}" = 0 ] || { echo; exit 0; }
    if [ "$(cat "$KINDLE_TEST_ROOT/app/launcher/runtime-target")" = colorsoft ] &&
        [ ! -s "$KINDLE_TEST_ROOT/home-actions" ]; then
        echo
    else
        echo com.lab126.booklet.home
    fi
    exit 0
fi
[ "$4" = load ] || { echo 1; exit 0; }
if [ -f "$KINDLE_TEST_ROOT/pending-module" ]; then
    [ "${KINDLE_TEST_SCREEN_QUERY_FAIL:-0}" = 0 ] || exit 1
    read -r module remaining <"$KINDLE_TEST_ROOT/pending-module"
    if [ "$remaining" = 0 ]; then
        updated=
        for current in $(cat "$KINDLE_TEST_ROOT/modules"); do
            [ "$current" = "$module" ] || updated="$updated $current"
        done
        printf '%s\n' "$updated" >"$KINDLE_TEST_ROOT/modules"
        rm "$KINDLE_TEST_ROOT/pending-module"
    elif [ "$remaining" -gt 0 ]; then
        printf '%s %s\n' "$module" "$((remaining - 1))" >"$KINDLE_TEST_ROOT/pending-module"
    fi
fi
cat "$KINDLE_TEST_ROOT/modules"
EOF
chmod +x "$TEST_DIR/tools/lipc-get-prop"
cat >"$TEST_DIR/tools/lipc-set-prop" <<'EOF'
#!/bin/sh
set -eu
if [ "$1" = com.lab126.appmgrd ]; then
    [ "$2" = start ] && [ "$3" = app://com.lab126.booklet.home ]
    if [ -f "$KINDLE_TEST_ROOT/home-pending" ]; then
        read -r remaining <"$KINDLE_TEST_ROOT/home-pending"
        if [ "$remaining" -gt 0 ]; then
            echo "$((remaining - 1))" >"$KINDLE_TEST_ROOT/home-pending"
            exit 1
        fi
    fi
    [ "${KINDLE_TEST_HOME_FAIL:-0}" = 0 ] || exit 1
    echo home >>"$KINDLE_TEST_ROOT/home-actions"
    exit 0
fi
case "$1" in
    com.lab126.pillow)
        case "$1 $2 $3" in
            'com.lab126.pillow disableEnablePillow disable')
                [ "${KINDLE_TEST_PILLOW_DISABLE_FAIL:-0}" = 0 ] || exit 1
                case "$(ps -o stat= -p "$KINDLE_TEST_AWESOME_PID")" in *T*) echo 'pillow disabled after awesome froze' >&2; exit 94 ;; esac ;;
            'com.lab126.pillow disableEnablePillow enable')
                [ "${KINDLE_TEST_PILLOW_ENABLE_FAIL:-0}" = 0 ] || exit 1
                case "$(ps -o stat= -p "$KINDLE_TEST_AWESOME_PID")" in *T*) echo 'pillow enabled while awesome frozen' >&2; exit 93 ;; esac ;;
            *) exit 1 ;;
        esac
        echo "$2 $3" >>"$KINDLE_TEST_ROOT/pause-actions"
        exit 0
        ;;
esac
printf '%s %s\n' "$4" "$5" >>"$KINDLE_TEST_ROOT/overlay-actions"
case "$4" in
    unload)
        [ "${KINDLE_TEST_SCREEN_UNLOAD_FAIL:-0}" = 0 ] || exit 1
        delay=${KINDLE_TEST_SCREEN_UNLOAD_DELAY:-0}
        [ "${KINDLE_TEST_SCREEN_STICKY:-0}" = 0 ] || delay=-1
        printf '%s %s\n' "$5" "$delay" >"$KINDLE_TEST_ROOT/pending-module"
        ;;
    load)
        if [ "${KINDLE_TEST_SCREEN_RESTORE_HANG:-0}" = 1 ]; then
            echo "$$" >"$KINDLE_TEST_ROOT/hung-command.pid"
            exec sleep 120
        fi
        [ "${KINDLE_TEST_SCREEN_RESTORE_FAIL:-0}" = 0 ] || exit 1
        rm -f "$KINDLE_TEST_ROOT/pending-module"
        printf '%s\n' "$5" >>"$KINDLE_TEST_ROOT/modules"
        ;;
    *) exit 1 ;;
esac
EOF
chmod +x "$TEST_DIR/tools/lipc-set-prop"
export PATH="$TEST_DIR/tools:$PATH"

run_case()
{
    mkdir "$TEST_DIR/run"
    cp "$SUPERVISOR" "$TEST_DIR/run/supervisor.sh"
    : >"$TEST_DIR/actions"
    : >"$TEST_DIR/delays"
    : >"$TEST_DIR/overlay-actions"
    : >"$TEST_DIR/home-actions"
    echo "${KINDLE_TEST_HOME_DELAY:-0}" >"$TEST_DIR/home-pending"
    : >"$TEST_DIR/pause-actions"
    rm -f "$TEST_DIR/pending-module"
    printf '%s\n' "${KINDLE_TEST_MODULES-screensaver langpicker blankwindow}" >"$TEST_DIR/modules"
    result=0
    sh "$TEST_DIR/run/supervisor.sh" "$TEST_DIR/app" "$TEST_DIR/run" "${KINDLE_TEST_MODE:-stop}" >"$TEST_DIR/run/launcher.log" 2>&1 || result=$?
    kill -0 "$KINDLE_TEST_VOLUME_PID"
    case "$(ps -o stat= -p "$KINDLE_TEST_VOLUME_PID")" in *T*) echo 'Volume daemon was not resumed.' >&2; exit 1 ;; esac
}

# A volume daemon already paused by another reader is not ours to resume.
printf '%s\n' 'screensaver langpicker blankwindow' >"$TEST_DIR/modules"
kill -STOP "$KINDLE_TEST_VOLUME_PID"
mkdir "$TEST_DIR/run"
cp "$SUPERVISOR" "$TEST_DIR/run/supervisor.sh"
: >"$TEST_DIR/actions"
result=0
sh "$TEST_DIR/run/supervisor.sh" "$TEST_DIR/app" "$TEST_DIR/run" "${KINDLE_TEST_MODE:-stop}" || result=$?
[ "$result" = 1 ] && [ ! -e "$TEST_DIR/run" ] && [ ! -s "$TEST_DIR/actions" ]
case "$(ps -o stat= -p "$KINDLE_TEST_VOLUME_PID")" in *T*) ;; *) echo 'Another reader lost its volume guard.' >&2; exit 1 ;; esac
kill -CONT "$KINDLE_TEST_VOLUME_PID"

run_case
[ "$result" = 0 ] && [ ! -e "$TEST_DIR/run" ]
[ "$(cat "$TEST_DIR/delays")" = delay ]
[ "$(cat "$TEST_DIR/actions")" = "$(printf 'stop\napp\nstart')" ]
[ "$(cat "$TEST_DIR/home-actions")" = home ]
# Home must activate even with frameworkStarted=0 and retry transient failures.
export KINDLE_TEST_HOME_DELAY=3
run_case
[ "$result" = 0 ]
[ ! -e "$TEST_DIR/run" ]
[ "$(cat "$TEST_DIR/home-pending")" = 0 ]
[ "$(cat "$TEST_DIR/home-actions")" = home ]
unset KINDLE_TEST_HOME_DELAY
for failure in KINDLE_TEST_HOME_FAIL; do
    export "$failure=1"
    : >"$TEST_DIR/app/logs/launcher.log"
    run_case
    [ "$result" = 0 ]
    [ -f "$TEST_DIR/run/supervisor.sh" ]
    [ ! -s "$TEST_DIR/home-actions" ]
    grep -q 'No stock Kindle application became active.' "$TEST_DIR/app/logs/launcher.log"
    grep -q 'Could not confirm an active Kindle Home.' "$TEST_DIR/app/logs/launcher.log"
    rm -rf "$TEST_DIR/run"
    unset "$failure"
done
# Other Kindle targets open Home themselves; recovery still waits for it.
echo pw3 >"$TEST_DIR/app/launcher/runtime-target"
run_case
[ "$result" = 0 ] && [ ! -e "$TEST_DIR/run" ] && [ ! -s "$TEST_DIR/home-actions" ]
grep -q 'Stock Kindle application active: com.lab126.booklet.home' "$TEST_DIR/app/logs/launcher.log"
# Supervisor lines carry UTC timestamps.
grep -Eq '^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z Kobalt supervisor started: target pw3, stock UI stop[.]$' "$TEST_DIR/app/logs/launcher.log"
# A restarted framework without an active application keeps the recovery state.
export KINDLE_TEST_NO_ACTIVE_APP=1
: >"$TEST_DIR/app/logs/launcher.log"
run_case
[ "$result" = 0 ] && [ -f "$TEST_DIR/run/supervisor.sh" ]
grep -q 'No stock Kindle application became active.' "$TEST_DIR/app/logs/launcher.log"
rm -rf "$TEST_DIR/run"
unset KINDLE_TEST_NO_ACTIVE_APP
echo colorsoft >"$TEST_DIR/app/launcher/runtime-target"
# Both overlays must finish their asynchronous unload before app startup.
export KINDLE_TEST_MODULES='splash screensaver langpicker blankwindow' KINDLE_TEST_SCREEN_UNLOAD_DELAY=2
run_case
[ "$result" = 0 ] && [ ! -e "$TEST_DIR/run" ]
[ "$(cat "$TEST_DIR/overlay-actions")" = "$(printf 'unload splash\nunload screensaver\nload screensaver')" ]
unset KINDLE_TEST_MODULES KINDLE_TEST_SCREEN_UNLOAD_DELAY
for failure in KINDLE_TEST_SCREEN_UNLOAD_FAIL KINDLE_TEST_SCREEN_QUERY_FAIL KINDLE_TEST_SCREEN_STICKY; do
    export "$failure=1"
    run_case
    [ "$result" = 1 ] && [ ! -e "$TEST_DIR/run" ]
    [ "$(cat "$TEST_DIR/actions")" = "$(printf 'stop\nstart')" ]
    unset "$failure"
done
# Execution can still fail after preflight, for example after a mount change.
chmod -x "$TEST_DIR/app/bin/kobalt"
run_case
[ "$result" = 126 ] && [ ! -e "$TEST_DIR/run" ]
[ "$(cat "$TEST_DIR/actions")" = "$(printf 'stop\nstart')" ]
chmod +x "$TEST_DIR/app/bin/kobalt"
export KINDLE_TEST_APP_EXIT=23
run_case
[ "$result" = 23 ] && [ ! -e "$TEST_DIR/run" ]
[ "$(cat "$TEST_DIR/actions")" = "$(printf 'stop\napp\nstart')" ]
export KINDLE_TEST_STOP_FAIL=1
run_case
[ "$result" = 1 ] && [ ! -e "$TEST_DIR/run" ]
[ ! -s "$TEST_DIR/delays" ]
[ "$(cat "$TEST_DIR/actions")" = stop ]
export KINDLE_TEST_STOP_FAIL=0 KINDLE_TEST_START_FAIL=1
run_case
[ "$result" = 23 ] && [ -f "$TEST_DIR/run/supervisor.sh" ]
[ "$(cat "$TEST_DIR/actions")" = "$(printf 'stop\napp\nstart')" ]
rm -rf "$TEST_DIR/run"

export KINDLE_TEST_START_FAIL=0 KINDLE_TEST_SCREEN_RESTORE_FAIL=1
run_case
[ "$result" = 23 ] && [ -f "$TEST_DIR/run/supervisor.sh" ]
[ "$(cat "$TEST_DIR/actions")" = "$(printf 'stop\napp\nstart')" ]
rm -rf "$TEST_DIR/run"
# A screensaver module that was absent before launch is not ours to reload.
export KINDLE_TEST_MODULES=
run_case
[ "$result" = 23 ] && [ ! -e "$TEST_DIR/run" ]
unset KINDLE_TEST_MODULES KINDLE_TEST_SCREEN_RESTORE_FAIL

# Hung stock commands must be reaped, leave recovery evidence, and never
# prevent trying GUI startup after a failed screensaver reload.
for failure in KINDLE_TEST_SCREEN_RESTORE_HANG KINDLE_TEST_START_HANG; do
    export "$failure=1"
    : >"$TEST_DIR/app/logs/launcher.log"
    run_case
    [ "$result" = 23 ] && [ -f "$TEST_DIR/run/supervisor.sh" ]
    [ "$(cat "$TEST_DIR/actions")" = "$(printf 'stop\napp\nstart')" ]
    grep -q 'Stock command timed out:' "$TEST_DIR/app/logs/launcher.log"
    grep -q 'Starting stock Kindle interface.' "$TEST_DIR/app/logs/launcher.log"
    if kill -0 "$(cat "$TEST_DIR/hung-command.pid")" 2>/dev/null; then
        echo 'Timed-out stock client was not reaped.' >&2; exit 1
    fi
    rm -rf "$TEST_DIR/run"
    unset "$failure"
done
# Pause mode keeps the stock UI running but frozen, as KOReader does.
cat >"$TEST_DIR/tools/pidof" <<'EOF'
#!/bin/sh
[ "${KINDLE_TEST_NO_AWESOME:-0}" = 0 ] || exit 1
echo "$KINDLE_TEST_AWESOME_PID"
EOF
chmod +x "$TEST_DIR/tools/pidof"
cat >"$TEST_DIR/app/bin/kobalt" <<'EOF'
#!/bin/sh
case "$(ps -o stat= -p "$KINDLE_TEST_VOLUME_PID")" in *T*) ;; *) exit 99 ;; esac
case "$(ps -o stat= -p "$KINDLE_TEST_AWESOME_PID")" in *T*) ;; *) echo 'Reader started with a live window manager.' >&2; exit 92 ;; esac
[ ! -s "$KINDLE_TEST_ROOT/actions" ] || { echo 'Pause mode stopped the framework.' >&2; exit 91; }
[ "${KOBALT_KINDLE_FRAMEWORK_RUNNING:-}" = 1 ] || { echo 'Power helper was not told the framework is running.' >&2; exit 90; }
echo app >>"$KINDLE_TEST_ROOT/actions"
printf 'reader frame' >"$KOBALT_KINDLE_FB_DEVICE"
exit "${KINDLE_TEST_APP_EXIT:-0}"
EOF
chmod +x "$TEST_DIR/app/bin/kobalt"
printf 'Kindle 5.16.2.1.1 (2907410035) {Mario} (Jan 1 2024)\n' >"$TEST_DIR/version"
export KOBALT_KINDLE_VERSION_FILE="$TEST_DIR/version" KOBALT_KINDLE_FB_DEVICE="$TEST_DIR/fb0" KINDLE_TEST_MODE=pause
export KINDLE_TEST_APP_EXIT=0
awesome_running()
{
    case "$(ps -o stat= -p "$KINDLE_TEST_AWESOME_PID")" in *T*) return 1 ;; esac
}
# Home is requested through appmgrd after pillow returns; home-actions records it.
pause_sequence=$(printf 'disableEnablePillow disable\ndisableEnablePillow enable')
printf 'stock screen' >"$TEST_DIR/fb0"
run_case
[ "$result" = 0 ] && [ ! -e "$TEST_DIR/run" ]
awesome_running
[ "$(cat "$TEST_DIR/actions")" = app ] && [ ! -s "$TEST_DIR/delays" ]
[ "$(cat "$TEST_DIR/pause-actions")" = "$pause_sequence" ]
[ "$(cat "$TEST_DIR/fb0")" = 'stock screen' ]
[ "$(cat "$TEST_DIR/home-actions")" = home ]
# Overlays still release before the app and the screensaver returns last.
export KINDLE_TEST_MODULES='splash screensaver langpicker blankwindow' KINDLE_TEST_SCREEN_UNLOAD_DELAY=2
run_case
[ "$result" = 0 ] && [ ! -e "$TEST_DIR/run" ] && awesome_running
[ "$(cat "$TEST_DIR/overlay-actions")" = "$(printf 'unload splash\nunload screensaver\nload screensaver')" ]
unset KINDLE_TEST_MODULES KINDLE_TEST_SCREEN_UNLOAD_DELAY
# Application failure still resumes the frozen UI.
export KINDLE_TEST_APP_EXIT=23
run_case
[ "$result" = 23 ] && [ ! -e "$TEST_DIR/run" ] && awesome_running
[ "$(cat "$TEST_DIR/pause-actions")" = "$pause_sequence" ]
# A failed overlay release still unwinds the pause.
export KINDLE_TEST_SCREEN_STICKY=1
run_case
[ "$result" = 1 ] && [ ! -e "$TEST_DIR/run" ] && awesome_running
[ ! -s "$TEST_DIR/actions" ]
[ "$(cat "$TEST_DIR/pause-actions")" = "$pause_sequence" ]
unset KINDLE_TEST_SCREEN_STICKY
# Without a readable framebuffer the frame restore is skipped, not fatal.
KOBALT_KINDLE_FB_DEVICE="$TEST_DIR/missing/fb0" run_case
[ "$result" = 23 ] && [ ! -e "$TEST_DIR/run" ] && awesome_running
# Pillow cannot be disabled: nothing froze and pillow is re-enabled anyway.
export KINDLE_TEST_PILLOW_DISABLE_FAIL=1
run_case
[ "$result" = 1 ] && [ ! -e "$TEST_DIR/run" ] && awesome_running
[ ! -s "$TEST_DIR/actions" ]
unset KINDLE_TEST_PILLOW_DISABLE_FAIL
# A window manager that another reader paused is not ours to resume.
kill -STOP "$KINDLE_TEST_AWESOME_PID"
run_case
[ "$result" = 1 ] && [ ! -e "$TEST_DIR/run" ] && [ ! -s "$TEST_DIR/actions" ] && [ ! -s "$TEST_DIR/pause-actions" ]
if awesome_running; then echo 'Another reader lost its window manager guard.' >&2; exit 1; fi
kill -CONT "$KINDLE_TEST_AWESOME_PID"
export KINDLE_TEST_NO_AWESOME=1
run_case
[ "$result" = 1 ] && [ ! -e "$TEST_DIR/run" ] && [ ! -s "$TEST_DIR/pause-actions" ]
unset KINDLE_TEST_NO_AWESOME
# Pillow stays disabled when it cannot be re-enabled; keep recovery files.
export KINDLE_TEST_PILLOW_ENABLE_FAIL=1
run_case
[ "$result" = 23 ] && [ -f "$TEST_DIR/run/supervisor.sh" ] && awesome_running
rm -rf "${TEST_DIR:?}/run"
unset KINDLE_TEST_PILLOW_ENABLE_FAIL
# Firmware older than 5.7.2, or an unreadable version, uses the stop path.
cat >"$TEST_DIR/app/bin/kobalt" <<'EOF'
#!/bin/sh
[ "${KOBALT_KINDLE_FRAMEWORK_RUNNING:-}" = 0 ] || { echo 'Fallback stop reported a running framework.' >&2; exit 88; }
echo app >>"$KINDLE_TEST_ROOT/actions"
exit "${KINDLE_TEST_APP_EXIT:-0}"
EOF
for version_text in 'Kindle 5.7.1 (123)' 'Kindle 4.1.4 (123)' ''; do
    printf '%s\n' "$version_text" >"$TEST_DIR/version"
    run_case
    [ "$result" = 23 ] && [ ! -e "$TEST_DIR/run" ] && awesome_running
    [ "$(cat "$TEST_DIR/actions")" = "$(printf 'stop\napp\nstart')" ]
    [ ! -s "$TEST_DIR/pause-actions" ]
done
rm -f "$TEST_DIR/version"
run_case
[ "$result" = 23 ] && [ "$(cat "$TEST_DIR/actions")" = "$(printf 'stop\napp\nstart')" ]
# TERM lets the app checkpoint, then resumes the paused UI.
printf 'Kindle 5.16.2.1.1 (1)\n' >"$TEST_DIR/version"
cat >"$TEST_DIR/app/bin/kobalt" <<'EOF'
#!/bin/sh
trap 'echo saved >>"$KINDLE_TEST_ROOT/actions"; exit 0' TERM
echo app >>"$KINDLE_TEST_ROOT/actions"
while :; do :; done
EOF
mkdir "$TEST_DIR/run"
cp "$SUPERVISOR" "$TEST_DIR/run/supervisor.sh"
: >"$TEST_DIR/actions"
: >"$TEST_DIR/pause-actions"
sh "$TEST_DIR/run/supervisor.sh" "$TEST_DIR/app" "$TEST_DIR/run" pause &
supervisor_pid=$!
attempt=0
until [ "$(tail -n 1 "$TEST_DIR/actions")" = app ]; do
    attempt=$((attempt + 1))
    [ "$attempt" -lt 100 ] || { kill -TERM "$supervisor_pid" 2>/dev/null || :; wait "$supervisor_pid" || :; exit 1; }
    sleep 0.01
done
if awesome_running; then echo 'Window manager was not frozen while the app ran.' >&2; exit 1; fi
kill -TERM "$supervisor_pid"
result=0
wait "$supervisor_pid" || result=$?
[ "$result" = 143 ] && [ ! -e "$TEST_DIR/run" ] && awesome_running
[ "$(cat "$TEST_DIR/actions")" = "$(printf 'app\nsaved')" ]
[ "$(cat "$TEST_DIR/pause-actions")" = "$pause_sequence" ]
unset KINDLE_TEST_MODE KINDLE_TEST_APP_EXIT KOBALT_KINDLE_VERSION_FILE KOBALT_KINDLE_FB_DEVICE

# Interrupt exactly after STOP, before its shell command returns. Recording
# the cleanup obligation afterward would leave the mock daemon frozen.
cat >"$TEST_DIR/interrupt-after-stop" <<'EOF'
#!/bin/sh
source=$1
shift
kill() {
    if command kill "$@"; then
        if [ "$1" = -STOP ]; then command kill -TERM "$$"; fi
        return 0
    fi
    return 1
}
. "$source"
EOF
mkdir "$TEST_DIR/run"
cp "$SUPERVISOR" "$TEST_DIR/run/supervisor.sh"
: >"$TEST_DIR/actions"
result=0
sh "$TEST_DIR/interrupt-after-stop" "$TEST_DIR/run/supervisor.sh" "$TEST_DIR/app" "$TEST_DIR/run" "${KINDLE_TEST_MODE:-stop}" || result=$?
[ "$result" = 143 ] && [ ! -e "$TEST_DIR/run" ] && [ ! -s "$TEST_DIR/actions" ]
case "$(ps -o stat= -p "$KINDLE_TEST_VOLUME_PID")" in *T*) echo 'Interrupted STOP leaked volume ownership.' >&2; exit 1 ;; esac

# An already stopped framework belongs to somebody else.
cat >"$TEST_DIR/tools/status" <<'EOF'
#!/bin/sh
if [ "$1" = volumd ]; then echo "volumd start/running, process $KINDLE_TEST_VOLUME_PID"; exit 0; fi
echo 'lab126_gui stop/waiting'
EOF
run_case
[ "$result" = 1 ] && [ ! -e "$TEST_DIR/run" ]
[ ! -s "$TEST_DIR/actions" ]

# TERM must let the app checkpoint before restoring the framework.
cat >"$TEST_DIR/tools/status" <<'EOF'
#!/bin/sh
if [ "$1" = volumd ]; then echo "volumd start/running, process $KINDLE_TEST_VOLUME_PID"; exit 0; fi
echo 'lab126_gui start/running, process 123'
EOF
cat >"$TEST_DIR/app/bin/kobalt" <<'EOF'
#!/bin/sh
trap 'echo saved >>"$KINDLE_TEST_ROOT/actions"; exit 0' TERM
echo app >>"$KINDLE_TEST_ROOT/actions"
while :; do :; done
EOF
export KINDLE_TEST_START_FAIL=0
mkdir "$TEST_DIR/run"
cp "$SUPERVISOR" "$TEST_DIR/run/supervisor.sh"
: >"$TEST_DIR/actions"
sh "$TEST_DIR/run/supervisor.sh" "$TEST_DIR/app" "$TEST_DIR/run" "${KINDLE_TEST_MODE:-stop}" &
supervisor_pid=$!
attempt=0
until [ "$(tail -n 1 "$TEST_DIR/actions")" = app ]; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 100 ]; then
        kill -TERM "$supervisor_pid" 2>/dev/null || :
        wait "$supervisor_pid" || :
        echo 'Timed out waiting for test app.' >&2
        exit 1
    fi
    sleep 0.01
done
kill -TERM "$supervisor_pid"
result=0
wait "$supervisor_pid" || result=$?
[ "$result" = 143 ] && [ ! -e "$TEST_DIR/run" ]
[ "$(cat "$TEST_DIR/actions")" = "$(printf 'stop\napp\nsaved\nstart')" ]
# A hung application cannot hold the stock UI and volume daemon forever.
cat >"$TEST_DIR/app/bin/kobalt" <<'EOF'
#!/bin/sh
trap '' TERM
echo app >>"$KINDLE_TEST_ROOT/actions"
while :; do :; done
EOF
mkdir "$TEST_DIR/run"
cp "$SUPERVISOR" "$TEST_DIR/run/supervisor.sh"
: >"$TEST_DIR/actions"
sh "$TEST_DIR/run/supervisor.sh" "$TEST_DIR/app" "$TEST_DIR/run" "${KINDLE_TEST_MODE:-stop}" &
supervisor_pid=$!
attempt=0
until [ "$(tail -n 1 "$TEST_DIR/actions")" = app ]; do
    attempt=$((attempt + 1))
    [ "$attempt" -lt 100 ] || { kill -TERM "$supervisor_pid"; wait "$supervisor_pid" || :; exit 1; }
    sleep 0.01
done
kill -TERM "$supervisor_pid"
result=0
wait "$supervisor_pid" || result=$?
[ "$result" = 143 ] && [ ! -e "$TEST_DIR/run" ]
[ "$(cat "$TEST_DIR/actions")" = "$(printf 'stop\napp\nstart')" ]
case "$(ps -o stat= -p "$KINDLE_TEST_VOLUME_PID")" in *T*) exit 1 ;; esac
echo 'Kindle supervisor checks passed.'
