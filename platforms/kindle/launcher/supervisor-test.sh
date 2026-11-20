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
if [ "${KINDLE_TEST_GUI_STOPPED:-0}" = 1 ]; then echo 'lab126_gui stop/waiting'; exit 0; fi
echo 'lab126_gui start/running, process 123'
EOF
# Kobalt never stops or restarts the stock framework. Record any attempt.
for tool in stop start; do
    printf '#!/bin/sh\necho %s >>"$KINDLE_TEST_ROOT/actions"\n' "$tool" >"$TEST_DIR/tools/$tool"
done
cat >"$TEST_DIR/tools/usleep" <<'EOF'
#!/bin/sh
case "$1" in
100000|1000000) sleep 0.001 ;;
*) exit 1 ;;
esac
EOF
cat >"$TEST_DIR/tools/pidof" <<'EOF'
#!/bin/sh
[ "${KINDLE_TEST_NO_AWESOME:-0}" = 0 ] || exit 1
echo "$KINDLE_TEST_AWESOME_PID"
EOF
cat >"$TEST_DIR/app/bin/kobalt" <<'EOF'
#!/bin/sh
case "$(ps -o stat= -p "$KINDLE_TEST_VOLUME_PID")" in *T*) ;; *) exit 99 ;; esac
case "$(ps -o stat= -p "$KINDLE_TEST_AWESOME_PID")" in *T*) ;; *) echo 'Reader started with a live window manager.' >&2; exit 92 ;; esac
[ "$(pwd)" = / ] || exit 98
case "${LD_LIBRARY_PATH:-}" in "$KINDLE_TEST_ROOT/app/lib"*) ;; *) exit 96 ;; esac
if grep -Eq '(^| )(splash|screensaver)( |$)' "$KINDLE_TEST_ROOT/modules"; then
    echo 'Reader started before stock overlays were released.' >&2
    exit 95
fi
echo app >>"$KINDLE_TEST_ROOT/actions"
mkdir "$KOBALT_KINDLE_RUN_DIR/waveform-abandoned"
printf 'reader frame' >"$KOBALT_KINDLE_FB_DEVICE"
exit "${KINDLE_TEST_APP_EXIT:-0}"
EOF
chmod +x "$TEST_DIR/tools/status" "$TEST_DIR/tools/start" "$TEST_DIR/tools/stop" "$TEST_DIR/tools/usleep" "$TEST_DIR/tools/pidof" "$TEST_DIR/app/bin/kobalt"
cat >"$TEST_DIR/tools/lipc-get-prop" <<'EOF'
#!/bin/sh
set -eu
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
case "$1 $2 ${3:-}" in
    'com.lab126.appmgrd start app://com.lab126.booklet.home')
        echo home >>"$KINDLE_TEST_ROOT/home-actions"
        exit 0
        ;;
    'com.lab126.pillow disableEnablePillow disable')
        [ "${KINDLE_TEST_PILLOW_DISABLE_FAIL:-0}" = 0 ] || exit 1
        case "$(ps -o stat= -p "$KINDLE_TEST_AWESOME_PID")" in *T*) echo 'pillow disabled after awesome froze' >&2; exit 94 ;; esac
        echo "$2 $3" >>"$KINDLE_TEST_ROOT/pause-actions"
        exit 0
        ;;
    'com.lab126.pillow disableEnablePillow enable')
        [ "${KINDLE_TEST_PILLOW_ENABLE_FAIL:-0}" = 0 ] || exit 1
        case "$(ps -o stat= -p "$KINDLE_TEST_AWESOME_PID")" in *T*) echo 'pillow enabled while awesome frozen' >&2; exit 93 ;; esac
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
printf 'Kindle 5.16.2.1.1 (2907410035) {Mario} (Jan 1 2024)\n' >"$TEST_DIR/version"
export KOBALT_KINDLE_VERSION_FILE="$TEST_DIR/version" KOBALT_KINDLE_FB_DEVICE="$TEST_DIR/fb0"
pause_sequence=$(printf 'disableEnablePillow disable\ndisableEnablePillow enable')

awesome_running()
{
    case "$(ps -o stat= -p "$KINDLE_TEST_AWESOME_PID")" in *T*) return 1 ;; esac
}

start_run()
{
    mkdir "$TEST_DIR/run"
    cp "$SUPERVISOR" "$TEST_DIR/run/supervisor.sh"
    : >"$TEST_DIR/actions"
    : >"$TEST_DIR/overlay-actions"
    : >"$TEST_DIR/home-actions"
    : >"$TEST_DIR/pause-actions"
    rm -f "$TEST_DIR/pending-module"
    printf '%s\n' "${KINDLE_TEST_MODULES-screensaver langpicker blankwindow}" >"$TEST_DIR/modules"
    printf 'stock screen' >"$TEST_DIR/fb0"
}

run_case()
{
    start_run
    result=0
    sh "$TEST_DIR/run/supervisor.sh" "$TEST_DIR/app" "$TEST_DIR/run" >"$TEST_DIR/run/launcher.log" 2>&1 || result=$?
    kill -0 "$KINDLE_TEST_VOLUME_PID"
    case "$(ps -o stat= -p "$KINDLE_TEST_VOLUME_PID")" in *T*) echo 'Volume daemon was not resumed.' >&2; exit 1 ;; esac
    awesome_running || { echo 'Window manager was not resumed.' >&2; exit 1; }
    if grep -qx 'stop\|start' "$TEST_DIR/actions"; then echo 'The stock framework was stopped or restarted.' >&2; exit 1; fi
}

# The launcher refuses the retired stop mode before it touches anything.
if sh "$(dirname "$SUPERVISOR")/launch.sh" --stop-ui 2>"$TEST_DIR/launch-errors"; then
    echo 'The launcher accepted --stop-ui.' >&2; exit 1
fi
grep -q 'no longer stops the stock UI' "$TEST_DIR/launch-errors"

# A volume daemon already paused by another reader is not ours to resume.
kill -STOP "$KINDLE_TEST_VOLUME_PID"
start_run
result=0
sh "$TEST_DIR/run/supervisor.sh" "$TEST_DIR/app" "$TEST_DIR/run" || result=$?
[ "$result" = 1 ] && [ ! -e "$TEST_DIR/run" ] && [ ! -s "$TEST_DIR/actions" ] && [ ! -s "$TEST_DIR/pause-actions" ]
case "$(ps -o stat= -p "$KINDLE_TEST_VOLUME_PID")" in *T*) ;; *) echo 'Another reader lost its volume guard.' >&2; exit 1 ;; esac
kill -CONT "$KINDLE_TEST_VOLUME_PID"

# The stock UI stays running but frozen, and returns with its own frame and Home.
: >"$TEST_DIR/app/logs/launcher.log"
run_case
[ "$result" = 0 ] && [ ! -e "$TEST_DIR/run" ]
[ "$(cat "$TEST_DIR/actions")" = app ]
[ "$(cat "$TEST_DIR/pause-actions")" = "$pause_sequence" ]
[ "$(cat "$TEST_DIR/fb0")" = 'stock screen' ]
[ "$(cat "$TEST_DIR/home-actions")" = home ]
# Supervisor lines carry UTC timestamps, including the memory samples.
grep -Eq '^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z Kobalt supervisor started: target colorsoft[.]$' "$TEST_DIR/app/logs/launcher.log"
grep -Eq '^[0-9]{4}-[0-9-]+T[0-9:]+Z memory with stock UI paused: ' "$TEST_DIR/app/logs/launcher.log"
# Overlays release before the app and the screensaver returns last.
export KINDLE_TEST_MODULES='splash screensaver langpicker blankwindow' KINDLE_TEST_SCREEN_UNLOAD_DELAY=2
run_case
[ "$result" = 0 ] && [ ! -e "$TEST_DIR/run" ]
[ "$(cat "$TEST_DIR/overlay-actions")" = "$(printf 'unload splash\nunload screensaver\nload screensaver')" ]
unset KINDLE_TEST_MODULES KINDLE_TEST_SCREEN_UNLOAD_DELAY
# A failed overlay release unwinds the pause without starting the app.
for failure in KINDLE_TEST_SCREEN_UNLOAD_FAIL KINDLE_TEST_SCREEN_QUERY_FAIL KINDLE_TEST_SCREEN_STICKY; do
    export "$failure=1"
    run_case
    [ "$result" = 1 ] && [ ! -e "$TEST_DIR/run" ] && [ ! -s "$TEST_DIR/actions" ]
    [ "$(cat "$TEST_DIR/pause-actions")" = "$pause_sequence" ]
    unset "$failure"
done
# Execution can still fail after preflight, for example after a mount change.
chmod -x "$TEST_DIR/app/bin/kobalt"
run_case
[ "$result" = 126 ] && [ ! -e "$TEST_DIR/run" ]
[ "$(cat "$TEST_DIR/pause-actions")" = "$pause_sequence" ]
chmod +x "$TEST_DIR/app/bin/kobalt"
# Application failure still resumes the frozen UI.
export KINDLE_TEST_APP_EXIT=23
run_case
[ "$result" = 23 ] && [ ! -e "$TEST_DIR/run" ]
[ "$(cat "$TEST_DIR/actions")" = app ] && [ "$(cat "$TEST_DIR/home-actions")" = home ]
# A failed screensaver reload still resumes the UI but keeps recovery files.
export KINDLE_TEST_SCREEN_RESTORE_FAIL=1
run_case
[ "$result" = 23 ] && [ -f "$TEST_DIR/run/supervisor.sh" ]
[ "$(cat "$TEST_DIR/pause-actions")" = "$pause_sequence" ]
rm -rf "${TEST_DIR:?}/run"
# A screensaver module that was absent before launch is not ours to reload.
export KINDLE_TEST_MODULES=
run_case
[ "$result" = 23 ] && [ ! -e "$TEST_DIR/run" ]
unset KINDLE_TEST_MODULES KINDLE_TEST_SCREEN_RESTORE_FAIL
# A hung screensaver reload is reaped, keeps recovery evidence and still
# resumes the UI.
export KINDLE_TEST_SCREEN_RESTORE_HANG=1
: >"$TEST_DIR/app/logs/launcher.log"
run_case
[ "$result" = 23 ] && [ -f "$TEST_DIR/run/supervisor.sh" ]
[ "$(cat "$TEST_DIR/pause-actions")" = "$pause_sequence" ]
grep -q 'Stock command timed out:' "$TEST_DIR/app/logs/launcher.log"
if kill -0 "$(cat "$TEST_DIR/hung-command.pid")" 2>/dev/null; then
    echo 'Timed-out stock client was not reaped.' >&2; exit 1
fi
rm -rf "${TEST_DIR:?}/run"
unset KINDLE_TEST_SCREEN_RESTORE_HANG
# Without a readable framebuffer the frame restore is skipped, not fatal.
KOBALT_KINDLE_FB_DEVICE="$TEST_DIR/missing/fb0" run_case
[ "$result" = 23 ] && [ ! -e "$TEST_DIR/run" ]
# Pillow cannot be disabled: nothing froze and pillow is re-enabled anyway.
export KINDLE_TEST_PILLOW_DISABLE_FAIL=1
run_case
[ "$result" = 1 ] && [ ! -e "$TEST_DIR/run" ] && [ ! -s "$TEST_DIR/actions" ]
unset KINDLE_TEST_PILLOW_DISABLE_FAIL
# Pillow stays disabled when it cannot be re-enabled; keep recovery files.
export KINDLE_TEST_PILLOW_ENABLE_FAIL=1
run_case
[ "$result" = 23 ] && [ -f "$TEST_DIR/run/supervisor.sh" ]
rm -rf "${TEST_DIR:?}/run"
unset KINDLE_TEST_PILLOW_ENABLE_FAIL
# A window manager that another reader paused is not ours to resume.
kill -STOP "$KINDLE_TEST_AWESOME_PID"
start_run
result=0
sh "$TEST_DIR/run/supervisor.sh" "$TEST_DIR/app" "$TEST_DIR/run" || result=$?
[ "$result" = 1 ] && [ ! -e "$TEST_DIR/run" ] && [ ! -s "$TEST_DIR/actions" ] && [ ! -s "$TEST_DIR/pause-actions" ]
if awesome_running; then echo 'Another reader lost its window manager guard.' >&2; exit 1; fi
kill -CONT "$KINDLE_TEST_AWESOME_PID"
export KINDLE_TEST_NO_AWESOME=1
run_case
[ "$result" = 1 ] && [ ! -e "$TEST_DIR/run" ] && [ ! -s "$TEST_DIR/pause-actions" ]
unset KINDLE_TEST_NO_AWESOME
# Firmware older than 5.7.2, or an unreadable version, cannot pause: refuse
# before taking anything.
: >"$TEST_DIR/app/logs/launcher.log"
for version_text in 'Kindle 5.7.1 (123)' 'Kindle 4.1.4 (123)' ''; do
    printf '%s\n' "$version_text" >"$TEST_DIR/version"
    run_case
    [ "$result" = 1 ] && [ ! -e "$TEST_DIR/run" ] && [ ! -s "$TEST_DIR/actions" ] && [ ! -s "$TEST_DIR/pause-actions" ]
done
grep -q 'cannot pause the stock UI' "$TEST_DIR/app/logs/launcher.log"
printf 'Kindle 5.16.2.1.1 (1)\n' >"$TEST_DIR/version"
# An already stopped framework belongs to somebody else.
export KINDLE_TEST_GUI_STOPPED=1
run_case
[ "$result" = 1 ] && [ ! -e "$TEST_DIR/run" ] && [ ! -s "$TEST_DIR/actions" ]
unset KINDLE_TEST_GUI_STOPPED
unset KINDLE_TEST_APP_EXIT

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
start_run
result=0
sh "$TEST_DIR/interrupt-after-stop" "$TEST_DIR/run/supervisor.sh" "$TEST_DIR/app" "$TEST_DIR/run" || result=$?
[ "$result" = 143 ] && [ ! -e "$TEST_DIR/run" ] && [ ! -s "$TEST_DIR/actions" ]
case "$(ps -o stat= -p "$KINDLE_TEST_VOLUME_PID")" in *T*) echo 'Interrupted STOP leaked volume ownership.' >&2; exit 1 ;; esac
awesome_running

# TERM lets the app checkpoint, then resumes the paused UI.
wait_for_app()
{
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
}
cat >"$TEST_DIR/app/bin/kobalt" <<'EOF'
#!/bin/sh
trap 'echo saved >>"$KINDLE_TEST_ROOT/actions"; exit 0' TERM
echo app >>"$KINDLE_TEST_ROOT/actions"
while :; do :; done
EOF
start_run
sh "$TEST_DIR/run/supervisor.sh" "$TEST_DIR/app" "$TEST_DIR/run" &
supervisor_pid=$!
wait_for_app
if awesome_running; then echo 'Window manager was not frozen while the app ran.' >&2; exit 1; fi
kill -TERM "$supervisor_pid"
result=0
wait "$supervisor_pid" || result=$?
[ "$result" = 143 ] && [ ! -e "$TEST_DIR/run" ] && awesome_running
[ "$(cat "$TEST_DIR/actions")" = "$(printf 'app\nsaved')" ]
[ "$(cat "$TEST_DIR/pause-actions")" = "$pause_sequence" ]
# A hung application cannot hold the stock UI and volume daemon forever.
cat >"$TEST_DIR/app/bin/kobalt" <<'EOF'
#!/bin/sh
trap '' TERM
echo app >>"$KINDLE_TEST_ROOT/actions"
while :; do :; done
EOF
start_run
sh "$TEST_DIR/run/supervisor.sh" "$TEST_DIR/app" "$TEST_DIR/run" &
supervisor_pid=$!
wait_for_app
kill -TERM "$supervisor_pid"
result=0
wait "$supervisor_pid" || result=$?
[ "$result" = 143 ] && [ ! -e "$TEST_DIR/run" ] && awesome_running
[ "$(cat "$TEST_DIR/actions")" = app ]
case "$(ps -o stat= -p "$KINDLE_TEST_VOLUME_PID")" in *T*) exit 1 ;; esac
echo 'Kindle supervisor checks passed.'
