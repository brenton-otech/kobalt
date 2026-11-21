#!/bin/sh

set -u
APP_DIR=$1
RUN_DIR=$2
# The stock UI keeps running but frozen while Kobalt runs, as KOReader does by
# default: pillow is disabled and the window manager is stopped with STOP.
shift 2
pillow_disabled=no
awesome_held=no
awesome_pids=
fb_dump=no
child=
exit_status=1
volume_held=no
volume_pid=
screensaver_loaded=no
recovery_complete=no
# The reader exits with this status to ask for an update and a restart.
UPDATE_STATUS=75
KPM=${KOBALT_KPM:-/var/local/kmc/bin/kpm}
# A stalled download must not hold the paused stock UI for ever. Ten ticks a second.
UPDATE_TICKS=${KOBALT_UPDATE_TICKS:-9000}
relaunch=no
cd / || exit 1

# Every supervisor line carries UTC time, so a device log can be ordered
# against sleep, USB and the reader's own events.
say()
{
    echo "$(date -u '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null) $*" >&2
}

# Stock LIPC clients and Upstart requests can stall too. In particular, a
# stuck screensaver reload must not prevent attempting to restore the GUI.
# Run only short-lived stock clients here, never the reader or its saves.
bounded_stock()
(
    trap - EXIT HUP INT TERM
    command_ticks=$1
    shift
    "$@" &
    command_pid=$!
    while kill -0 "$command_pid" 2>/dev/null; do
        if [ "$command_ticks" -le 0 ]; then
            say "Stock command timed out: $*"
            kill -KILL "$command_pid" 2>/dev/null || :
            wait "$command_pid" 2>/dev/null || :
            return 124
        fi
        usleep 100000 || :
        command_ticks=$((command_ticks - 1))
    done
    wait "$command_pid"
)

has_module()
{
    wanted_module=$1
    modules=$(bounded_stock 50 lipc-get-prop -s -q com.lab126.blanket load) || return 2
    for loaded_module in $modules; do
        [ "$loaded_module" != "$wanted_module" ] || return 0
    done
    return 1
}

wait_for_module()
{
    module_name=$1
    module_expected=$2
    module_attempts=0
    while [ "$module_attempts" -lt 50 ]; do
        if has_module "$module_name"; then module_status=0; else module_status=$?; fi
        [ "$module_status" != 2 ] || return 1
        if { [ "$module_expected" = yes ] && [ "$module_status" = 0 ]; } ||
           { [ "$module_expected" = no ] && [ "$module_status" = 1 ]; }; then
            return 0
        fi
        usleep 100000 || return 1
        module_attempts=$((module_attempts + 1))
    done
    return 1
}

# Record system memory in launcher.log so a device run shows what holding the
# stock UI costs. Best effort: this never affects the launch.
log_memory()
{
    [ -r /proc/meminfo ] || return 0
    say "memory $1: $(grep -E '^(MemFree|MemAvailable):' /proc/meminfo 2>/dev/null | tr -s ' \n' ' ')"
}

# Compare dotted firmware versions on their first three numeric fields.
version_ge()
{
    version_have=$1
    version_want=$2
    old_ifs=$IFS
    IFS=.
    set -- $version_have
    have_a=${1:-0}; have_b=${2:-0}; have_c=${3:-0}
    set -- $version_want
    want_a=${1:-0}; want_b=${2:-0}; want_c=${3:-0}
    IFS=$old_ifs
    for version_field in "$have_a" "$have_b" "$have_c" "$want_a" "$want_b" "$want_c"; do
        case "$version_field" in ''|*[!0-9]*) return 1 ;; esac
    done
    [ "$have_a" -eq "$want_a" ] || { [ "$have_a" -gt "$want_a" ]; return; }
    [ "$have_b" -eq "$want_b" ] || { [ "$have_b" -gt "$want_b" ]; return; }
    [ "$have_c" -ge "$want_c" ]
}

# KOReader's pause flow needs pillow's disable switch (FW 5.6.5) and a window
# manager it can freeze (FW 5.7.2). Older firmware keeps the stop path.
pause_supported()
{
    [ -r "${KOBALT_KINDLE_VERSION_FILE:-/etc/prettyversion.txt}" ] || return 1
    fw_version=$(sed -n -e '/^Kindle 5/s/^Kindle[[:space:]]*\([0-9.]*\).*$/\1/p' "${KOBALT_KINDLE_VERSION_FILE:-/etc/prettyversion.txt}" | head -n 1)
    [ -n "$fw_version" ] && version_ge "$fw_version" 5.7.2
}

restore_screensaver()
{
    [ "$screensaver_loaded" = yes ] || return 0
    bounded_stock 50 lipc-set-prop -s -q com.lab126.blanket load screensaver || return 1
    wait_for_module screensaver yes
}

release_overlays()
{
    # Use short-lived stock clients before starting the app. Polling blanket
    # through the power helper's live service connection times out on PW3.
    for release_module in splash screensaver; do
        if has_module "$release_module"; then
            lipc-set-prop -s -q com.lab126.blanket unload "$release_module" || return 1
            wait_for_module "$release_module" no || return 1
        else
            [ "$?" = 1 ] || return 1
        fi
    done
}

stop_bridge()
{
    [ -f "$RUN_DIR/bridge.pid" ] || return 0
    bridge_pid=$(cat "$RUN_DIR/bridge.pid")
    case "$bridge_pid" in ''|*[!0-9]*) return 1 ;; esac
    # Do not signal a recycled PID after the helper has already exited.
    if [ "$(readlink "/proc/$bridge_pid/exe" 2>/dev/null)" = "$RUN_DIR/power-bridge" ]; then
        kill -TERM "$bridge_pid" 2>/dev/null || :
        attempts=0
        while kill -0 "$bridge_pid" 2>/dev/null && [ "$attempts" -lt 3 ]; do
            usleep 1000000 || :
            attempts=$((attempts + 1))
        done
        if [ "$(readlink "/proc/$bridge_pid/exe" 2>/dev/null)" = "$RUN_DIR/power-bridge" ]; then
            kill -KILL "$bridge_pid" 2>/dev/null || :
        fi
    fi
}

restore_waveform_mounts()
{
    waveform_restored=yes
    for probe in "$RUN_DIR"/waveform-*; do
        [ -e "$probe" ] || [ -L "$probe" ] || continue
        if [ ! -d "$probe" ] || [ -L "$probe" ]; then
            waveform_restored=no
            say "Unexpected waveform recovery entry: $probe"
            continue
        fi
        if [ -r /proc/mounts ] && grep -Fq " $probe " /proc/mounts; then
            if ! umount -i "$probe"; then
                waveform_restored=no
                say "Could not unmount read-only waveform probe: $probe"
                continue
            fi
        fi
        if ! rmdir "$probe"; then
            waveform_restored=no
            say "Could not remove waveform probe directory: $probe"
        fi
    done
}

recover()
{
    trap '' HUP INT TERM
    say "Kobalt recovery started (app status $exit_status)."
    if [ -n "$child" ]; then
        kill -TERM "$child" 2>/dev/null || :
        attempts=0
        while kill -0 "$child" 2>/dev/null && [ "$attempts" -lt 100 ]; do
            usleep 100000 || :
            attempts=$((attempts + 1))
        done
        if kill -0 "$child" 2>/dev/null; then
            say 'Kobalt did not stop within ten seconds; forcing exit before recovery.'
            kill -KILL "$child" 2>/dev/null || :
        fi
        wait "$child" 2>/dev/null || :
    fi
    stop_bridge || say 'Could not inspect the power bridge PID.'
    restore_waveform_mounts
    if [ "$volume_held" = yes ]; then
        if kill -CONT "$volume_pid" 2>/dev/null; then
            volume_held=no
        else
            say "Could not resume volumd PID $volume_pid. Recovery files remain in $RUN_DIR."
        fi
    fi
    if [ "$awesome_held" = yes ] || [ "$pillow_disabled" = yes ]; then
        screen_restored=yes
        if ! restore_screensaver; then
            screen_restored=no
            say 'Could not reload stock screensaver. Recovery files remain.'
        fi
        if [ "$awesome_held" = yes ]; then
            awesome_resumed=yes
            for awesome_pid in $awesome_pids; do
                # A window manager that died while paused has nothing to resume.
                if kill -0 "$awesome_pid" 2>/dev/null && ! kill -CONT "$awesome_pid" 2>/dev/null; then
                    awesome_resumed=no
                fi
            done
            if [ "$awesome_resumed" = yes ]; then
                awesome_held=no
            else
                say "Could not resume awesome PIDs $awesome_pids. Run 'kill -CONT $awesome_pids' from SSH."
                return
            fi
        fi
        if [ "$pillow_disabled" = yes ]; then
            # Leave the panel with the stock screen from before launch rather
            # than the last page, then let pillow and Home redraw over it.
            if [ "$fb_dump" = yes ]; then
                cat "$RUN_DIR/fb.dump" >"${KOBALT_KINDLE_FB_DEVICE:-/dev/fb0}" 2>/dev/null || :
            fi
            if bounded_stock 50 lipc-set-prop com.lab126.pillow disableEnablePillow enable; then
                pillow_disabled=no
                bounded_stock 50 lipc-set-prop com.lab126.appmgrd start app://com.lab126.booklet.home ||
                    say 'Could not ask the stock UI to show Home.'
            else
                say "Could not enable pillow. Run 'lipc-set-prop com.lab126.pillow disableEnablePillow enable' from SSH."
                return
            fi
        fi
        [ "$screen_restored" = yes ] || return
    fi
    [ "$volume_held" = no ] || return
    [ "$waveform_restored" = yes ] || return
    recovery_complete=yes
}
# Installs the newest Kobalt while the stock UI stays paused, so no stock
# screen shows between the old build and the new one. Returns 0 when the
# installed build can start under this supervisor. KPM's hooks refuse while
# the run folder exists unless "updating" names this living process.
apply_update()
{
    say 'Update requested; installing with the stock UI paused.'
    stop_bridge || say 'Could not inspect the power bridge PID.'
    rm -f "$RUN_DIR/bridge.pid"
    restore_waveform_mounts
    if [ ! -x "$KPM" ]; then
        say "KPM is missing at $KPM; the update was not attempted."
    else
        echo "$$" >"$RUN_DIR/updating"
        # install takes the newest kobalt alone; upgrade would move every package.
        bounded_stock "$UPDATE_TICKS" "$KPM" -y install kobalt >&2
        say "kpm install kobalt exited with status $?."
        rm -f "$RUN_DIR/updating"
    fi
    # A failed install can leave no app at all. Then only the stock UI can return.
    if [ ! -x "$APP_DIR/bin/kobalt" ] || [ ! -x "$APP_DIR/bin/kobalt-power-bridge" ] ||
       ! sh "$APP_DIR/launcher/check-runtime.sh" "$APP_DIR"; then
        say 'The installed Kobalt cannot start; returning to the stock UI.'
        return 1
    fi
    # This shell still runs the previous build's supervisor. A build that
    # changed it starts through its own launcher, after the stock UI returns.
    if ! cmp -s "$APP_DIR/launcher/supervisor.sh" "$RUN_DIR/supervisor.sh"; then
        say 'The update changed the supervisor; restarting through the launcher.'
        relaunch=yes
        return 1
    fi
    if ! cp "$APP_DIR/bin/kobalt-power-bridge" "$RUN_DIR/power-bridge.new" ||
       ! chmod 700 "$RUN_DIR/power-bridge.new" ||
       ! mv -f "$RUN_DIR/power-bridge.new" "$RUN_DIR/power-bridge"; then
        say 'Could not replace the power bridge; restarting through the launcher.'
        relaunch=yes
        return 1
    fi
    # The Kindle may have shown its screensaver while the update ran.
    if ! release_overlays; then
        say 'Could not release stock screen overlays; restarting through the launcher.'
        relaunch=yes
        return 1
    fi
    return 0
}
cleanup()
{
    recover
    # Include recovery failures before removing the system-storage log. Keep
    # the full recovery directory when any restoration step failed.
    if [ -f "$RUN_DIR/launcher.log" ]; then
        cat "$RUN_DIR/launcher.log" >>"$APP_DIR/logs/launcher.log" 2>/dev/null || :
    fi
    [ "$recovery_complete" = yes ] || return
    rm -f "$RUN_DIR/supervisor.sh" "$RUN_DIR/pid" "$RUN_DIR/bridge.pid" "$RUN_DIR/power-bridge" "$RUN_DIR/volume.pid" "$RUN_DIR/launcher.log" "$RUN_DIR/screensaver.loaded" "$RUN_DIR/awesome.pid" "$RUN_DIR/fb.dump" "$RUN_DIR/updating" "$RUN_DIR/power-bridge.new"
    rmdir "$RUN_DIR" || return
    if [ "$relaunch" = yes ]; then
        nohup sh "$APP_DIR/launcher/launch.sh" >>"$APP_DIR/logs/launcher.log" 2>&1 </dev/null &
    fi
}
terminate()
{
    # cleanup owns the child's single TERM and bounded checkpoint wait.
    # Sending TERM here as well can interrupt its shutdown handler twice.
    exit 143
}
trap cleanup EXIT
trap terminate INT TERM
trap '' HUP
echo "$$" >"$RUN_DIR/pid"
say "Kobalt supervisor started: target $(cat "$APP_DIR/launcher/runtime-target" 2>/dev/null)."

state=$(status lab126_gui 2>&1) || { say "$state"; exit 1; }
case "$state" in
    *'start/running'*) ;;
    *) say "Refusing to launch: lab126_gui is not running: $state"; exit 1 ;;
esac

if has_module screensaver; then
    screensaver_loaded=yes
else
    module_status=$?
    [ "$module_status" != 2 ] || { say 'Cannot inspect stock screensaver modules.'; exit 1; }
fi
echo "$screensaver_loaded" >"$RUN_DIR/screensaver.loaded"

# Pausing needs pillow's disable switch (FW 5.6.5) and a window manager it
# can freeze (FW 5.7.2). Older or unreadable firmware is refused.
if ! pause_supported; then
    say 'Refusing to launch: this firmware cannot pause the stock UI (5.7.2 or newer is needed).'
    exit 1
fi

# Freeze only the current stock volume daemon. It must not unmount USB
# storage while Kobalt still maps its executable, Qt libraries and profile.
volume_state=$(status volumd 2>&1) || { say "$volume_state"; exit 1; }
case "$volume_state" in
    *'start/running, process '*) volume_pid=${volume_state##*process } ;;
    *) say "Refusing to launch: volumd is not running: $volume_state"; exit 1 ;;
esac
case "$volume_pid" in ''|*[!0-9]*) say 'Invalid volumd PID.'; exit 1 ;; esac
# Upstart still reports running when another reader has paused the process.
# Refuse that state rather than taking ownership and later resuming its guard.
volume_process_state=$(ps -o stat= -p "$volume_pid") || exit 1
case "$volume_process_state" in
    ''|*T*|*t*|*Z*|*X*) say 'Refusing to launch: volumd is stopped or unavailable.'; exit 1 ;;
esac
echo "$volume_pid" >"$RUN_DIR/volume.pid"
KOBALT_KINDLE_USB_NETWORK=$(lipc-get-prop -i -q com.lab126.volumd useUsbForNetwork) || KOBALT_KINDLE_USB_NETWORK=0
export KOBALT_KINDLE_USB_NETWORK
# Record the cleanup obligation before STOP. TERM can arrive immediately
# after the signal, before the shell executes its next assignment.
volume_held=yes
kill -STOP "$volume_pid" || exit 1

log_memory 'before stock UI pause'
# Freeze exactly the window manager that is running now. Refuse one that
# another reader already paused, as with volumd.
awesome_pids=$(pidof awesome) || awesome_pids=
[ -n "$awesome_pids" ] || { say 'Refusing to launch: awesome is not running.'; exit 1; }
for awesome_pid in $awesome_pids; do
    case "$awesome_pid" in ''|*[!0-9]*) say 'Invalid awesome PID.'; exit 1 ;; esac
    awesome_state=$(ps -o stat= -p "$awesome_pid") || exit 1
    case "$awesome_state" in
        ''|*T*|*t*|*Z*|*X*) say 'Refusing to launch: awesome is stopped or unavailable.'; exit 1 ;;
    esac
done
echo "$awesome_pids" >"$RUN_DIR/awesome.pid"
if cat "${KOBALT_KINDLE_FB_DEVICE:-/dev/fb0}" >"$RUN_DIR/fb.dump" 2>/dev/null; then
    fb_dump=yes
else
    rm -f "$RUN_DIR/fb.dump"
fi
# Record each cleanup obligation before its action, as with volumd.
pillow_disabled=yes
bounded_stock 50 lipc-set-prop com.lab126.pillow disableEnablePillow disable || { say 'Could not disable pillow; Kobalt was not started.'; exit 1; }
awesome_held=yes
# shellcheck disable=SC2086
kill -STOP $awesome_pids || { say 'Could not pause awesome; Kobalt was not started.'; exit 1; }

if ! release_overlays; then
    say 'Could not release stock screen overlays; Kobalt was not started.'
    exit 1
fi

log_memory 'with stock UI paused'
# Stock recovery commands must keep their original library environment even
# when the app cannot execute or its bundled libraries cannot be mapped.
while :; do
    QT_QPA_PLATFORM=kindle \
    QT_QPA_PLATFORM_PLUGIN_PATH="$APP_DIR/plugins/platforms" \
    QT_PLUGIN_PATH="$APP_DIR/plugins" \
    QT_QPA_FONTDIR="$APP_DIR/assets/fonts" \
    LD_LIBRARY_PATH="$APP_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    KOBALT_KINDLE_HOME="$APP_DIR" \
    KOBALT_KINDLE_RUN_DIR="$RUN_DIR" \
    KOBALT_KINDLE_POWER_BRIDGE="$RUN_DIR/power-bridge" \
    "$APP_DIR/bin/kobalt" --assets "$APP_DIR/assets" "$@" &
    child=$!
    wait "$child"
    exit_status=$?
    say "Kobalt process exited (status $exit_status)."
    child=
    [ "$exit_status" = "$UPDATE_STATUS" ] || break
    # The request itself is not a failure, whatever the update then does.
    exit_status=0
    apply_update || break
    say 'Starting the installed Kobalt.'
done
exit "$exit_status"
