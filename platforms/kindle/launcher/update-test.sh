#!/bin/sh
# Checks the update helper with a fake KPM and launcher: it waits for the run
# folder, installs kobalt alone, starts Kobalt after success and after a failed
# install, and neither installs nor starts while Kobalt never exits.
set -eu
here=$(CDPATH='' cd "$(dirname "$0")" && pwd -P)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }

setup()
{
    rm -rf "$work/app" "$work/run" "$work/calls"
    mkdir -p "$work/app/launcher" "$work/run"
    printf '#!/bin/sh\necho "kpm $*" >>"%s/calls"\nexit %s\n' "$work" "$1" >"$work/kpm"
    chmod +x "$work/kpm"
    printf '#!/bin/sh\necho launch >>"%s/calls"\n' "$work" >"$work/app/launcher/launch.sh"
}

# The helper waits for the run folder to go, then installs and starts.
setup 0
(sleep 1; rmdir "$work/run") &
KOBALT_KPM="$work/kpm" sh "$here/update.sh" "$work/app" "$work/run"
wait
[ "$(cat "$work/calls")" = "kpm -y install kobalt
launch" ] || fail "a successful update did not install then start: $(cat "$work/calls")"
grep -q 'status 0' "$work/app/logs/update.log" || fail 'the log lacks the KPM status'

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

# A reader that never exits is left alone.
setup 0
if KOBALT_KPM="$work/kpm" KOBALT_UPDATE_WAIT_TICKS=3 sh "$here/update.sh" "$work/app" "$work/run"; then
    fail 'the helper succeeded while Kobalt was still running'
fi
[ ! -e "$work/calls" ] || fail 'the helper acted while Kobalt was still running'
echo 'Update helper passed.'
