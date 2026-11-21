#!/bin/sh
# Checks the update helper with a fake KPM and launcher: it waits for the run
# folder, installs kobalt alone, starts Kobalt after success and after a failed
# install, and neither installs nor starts while Kobalt never exits. While KPM
# runs, the real launcher refuses to start Kobalt; a stale lock does not.
set -eu
here=$(CDPATH='' cd "$(dirname "$0")" && pwd -P)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
KOBALT_UPDATE_LOCK=$work/lock/active
export KOBALT_UPDATE_LOCK

# The fake KPM records its call, whether the lock is held and what the real
# launcher answers at that moment.
setup()
{
    rm -rf "$work/app" "$work/run" "$work/calls" "$work/lock"
    mkdir -p "$work/app/launcher" "$work/run"
    cat >"$work/kpm" <<FAKE
#!/bin/sh
echo "kpm \$*" >>"$work/calls"
[ -d "$KOBALT_UPDATE_LOCK" ] && echo locked >>"$work/calls"
sh "$here/launch.sh" >/dev/null 2>"$work/refusal" && echo started >>"$work/calls"
exit $1
FAKE
    chmod +x "$work/kpm"
    printf '#!/bin/sh\n[ -d "%s" ] && echo "launch while locked" >>"%s/calls"\necho launch >>"%s/calls"\n' \
        "$KOBALT_UPDATE_LOCK" "$work" "$work" >"$work/app/launcher/launch.sh"
}

# The helper waits for the run folder to go, then installs and starts.
setup 0
(sleep 1; rmdir "$work/run") &
KOBALT_KPM="$work/kpm" sh "$here/update.sh" "$work/app" "$work/run"
wait
[ "$(cat "$work/calls")" = "kpm -y install kobalt
locked
launch" ] || fail "a successful update did not install under the lock, then start: $(cat "$work/calls")"
grep -q 'Kobalt is updating' "$work/refusal" || fail 'the launcher did not refuse a start during the update'
grep -q 'status 0' "$work/app/logs/update.log" || fail 'the log lacks the KPM status'
[ ! -e "$KOBALT_UPDATE_LOCK" ] || fail 'the lock outlived the update'

# A failed install still starts the installed Kobalt.
setup 2
rmdir "$work/run"
KOBALT_KPM="$work/kpm" sh "$here/update.sh" "$work/app" "$work/run"
[ "$(tail -1 "$work/calls")" = launch ] || fail 'a failed install did not start Kobalt'

# A missing KPM starts Kobalt without an install.
setup 0
rmdir "$work/run"
KOBALT_KPM="$work/missing" sh "$here/update.sh" "$work/app" "$work/run"
[ "$(cat "$work/calls")" = launch ] || fail 'a missing KPM did not just start Kobalt'

# A reader that never exits is left alone, and the lock is released.
setup 0
if KOBALT_KPM="$work/kpm" KOBALT_UPDATE_WAIT_TICKS=3 sh "$here/update.sh" "$work/app" "$work/run"; then
    fail 'the helper succeeded while Kobalt was still running'
fi
[ ! -e "$work/calls" ] || fail 'the helper acted while Kobalt was still running'
[ ! -e "$KOBALT_UPDATE_LOCK" ] || fail 'a cancelled update kept its lock'

# A lock whose helper is gone does not block the launcher: it is removed.
mkdir -p "$KOBALT_UPDATE_LOCK"
sh -c 'echo $$' >"$KOBALT_UPDATE_LOCK/pid"
sh "$here/launch.sh" >/dev/null 2>"$work/refusal" || :
! grep -q 'Kobalt is updating' "$work/refusal" || fail 'a stale lock blocked the launcher'
[ ! -e "$KOBALT_UPDATE_LOCK" ] || fail 'a stale lock was kept'
echo 'Update helper passed.'
