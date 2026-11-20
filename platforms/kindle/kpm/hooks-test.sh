#!/bin/sh
# Runs the KPM hooks the way KPM does: from a freshly extracted package folder,
# with sh, and with "upgrade" when replacing an installed version.
set -eu
HOOKS=$(CDPATH='' cd "$(dirname "$0")" && pwd -P)
mkdir -p "${1:?pass an ignored scratch directory}"
scratch=$(mktemp -d "$1/kpm-test.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
export KOBALT_KPM_APP="$scratch/us/kobalt" KOBALT_KPM_DOCUMENTS="$scratch/us/documents"
export KOBALT_KPM_RUN_DIR="$scratch/run"
app=$KOBALT_KPM_APP
fail() { echo "KPM hook check failed: $*" >&2; exit 1; }

# Extract a package with the given extra library into a fresh package folder.
unpack() {
    package="$scratch/packages/kobalt"
    rm -rf "$package"
    mkdir -p "$package/scriptlets" "$package/kobalt/bin" "$package/kobalt/launcher" "$package/kobalt/lib/$1"
    cp "$HOOKS/install.sh" "$HOOKS/uninstall.sh" "$HOOKS/launch.sh" "$package/"
    cp "$HOOKS/Kobalt.sh" "$package/scriptlets/"
    echo "kobalt $2" >"$package/kobalt/bin/kobalt"
    echo launcher >"$package/kobalt/launcher/launch.sh"
    echo "$1" >"$package/kobalt/lib/$1/lib$1.so"
}
hook() { (cd "$scratch/packages/kobalt" && sh "$@"); }

# A ZIP install left the app, settings, progress and logs behind.
mkdir -p "$app/data" "$app/logs" "$app/bin"
echo settings >"$app/data/settings.sqlite"
echo log >"$app/logs/launcher.log"
echo old >"$app/bin/kobalt"
unpack first 1
hook install.sh >/dev/null
[ "$(cat "$app/bin/kobalt")" = 'kobalt 1' ] || fail 'install did not replace the app'
[ -f "$app/lib/first/libfirst.so" ] || fail 'install missed a library'
[ "$(cat "$app/data/settings.sqlite")" = settings ] || fail 'install changed data'
[ "$(cat "$app/logs/launcher.log")" = log ] || fail 'install changed logs'
cmp -s "$HOOKS/Kobalt.sh" "$KOBALT_KPM_DOCUMENTS/Kobalt.sh" || fail 'scriptlet missing'
grep -q '^data/\|^logs/' "$app/kpm-installed.txt" && fail 'install recorded app-owned files'
[ -z "$(find "$scratch/packages/kobalt/kobalt" -type f)" ] || fail 'install copied instead of moving'

# Kobalt is running: neither hook may change anything.
mkdir "$KOBALT_KPM_RUN_DIR"
unpack second 2
hook uninstall.sh upgrade 2>/dev/null && fail 'upgrade proceeded while Kobalt runs'
hook install.sh upgrade 2>/dev/null && fail 'install proceeded while Kobalt runs'
hook uninstall.sh 2>/dev/null && fail 'uninstall proceeded while Kobalt runs'
[ "$(cat "$app/bin/kobalt")" = 'kobalt 1' ] || fail 'a refused hook changed the app'
rmdir "$KOBALT_KPM_RUN_DIR"

# Upgrade: KPM runs the old uninstall hook, then the new install hook.
unpack first 1
hook uninstall.sh upgrade >/dev/null
[ -f "$app/bin/kobalt" ] || fail 'upgrade uninstall removed the app'
unpack second 2
hook install.sh upgrade >/dev/null
[ "$(cat "$app/bin/kobalt")" = 'kobalt 2' ] || fail 'upgrade did not replace the app'
[ -f "$app/lib/second/libsecond.so" ] || fail 'upgrade missed a new library'
[ ! -e "$app/lib/first" ] || fail 'upgrade kept a retired library'
[ "$(cat "$app/data/settings.sqlite")" = settings ] || fail 'upgrade changed data'

# Uninstall removes only what the package installed.
echo mine >"$app/notes.txt"
echo '# changed' >>"$KOBALT_KPM_DOCUMENTS/Kobalt.sh"
hook uninstall.sh >/dev/null
[ ! -e "$app/bin" ] && [ ! -e "$app/lib" ] && [ ! -e "$app/launcher" ] || fail 'uninstall left app files'
[ ! -e "$app/kpm-installed.txt" ] || fail 'uninstall left its file list'
[ "$(cat "$app/data/settings.sqlite")" = settings ] || fail 'uninstall removed data'
[ "$(cat "$app/logs/launcher.log")" = log ] || fail 'uninstall removed logs'
[ -f "$app/notes.txt" ] || fail 'uninstall removed a file it did not install'
[ -f "$KOBALT_KPM_DOCUMENTS/Kobalt.sh" ] || fail 'uninstall removed a changed scriptlet'

# A clean reinstall and uninstall leave no app folder or scriptlet behind.
rm -rf "$app" "$KOBALT_KPM_DOCUMENTS/Kobalt.sh"
unpack first 1
hook install.sh >/dev/null
hook uninstall.sh >/dev/null
[ ! -e "$app" ] || fail 'uninstall left an empty app folder'
[ ! -e "$KOBALT_KPM_DOCUMENTS/Kobalt.sh" ] || fail 'uninstall left the scriptlet'
echo 'Kindle KPM hook checks passed.'
