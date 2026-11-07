#!/bin/sh

# KPM runs this hook with sh from the unpacked package folder, after a fresh
# extraction. On an upgrade it passes "upgrade"; the previous package's
# uninstall hook has then kept the installed app. Move the app files into
# /mnt/us/kobalt, never touching data/ (settings and progress) or logs/.
set -eu
APP=${KOBALT_KPM_APP:-/mnt/us/kobalt}
DOCUMENTS=${KOBALT_KPM_DOCUMENTS:-/mnt/us/documents}
RUN_DIR=${KOBALT_KPM_RUN_DIR:-/var/tmp/kobalt-kindle-prototype}
LIST="$APP/kpm-installed.txt"
SCRIPTLET=Kobalt.sh

if [ -d "$RUN_DIR" ]; then
    echo "Kobalt is running, or its previous supervisor failed ($RUN_DIR). Exit Kobalt first." >&2
    exit 1
fi
[ -f kobalt/bin/kobalt ] && [ -f kobalt/launcher/launch.sh ] || {
    echo 'The Kobalt package is incomplete.' >&2
    exit 1
}

# Paths relative to the app folder, one per line. The app owns data/ and
# logs/, so the package never lists them.
(cd kobalt && find . -type f | sed 's|^\./||' | grep -Ev '^(data|logs)/' | sort) >kobalt-files.txt
mkdir -p "$APP"
# Record the union first: if this hook fails part-way, KPM runs the uninstall
# hook, which then removes both the earlier files and any already moved here.
{
    [ ! -f "$LIST" ] || cat "$LIST"
    cat kobalt-files.txt
} | sort -u >"$LIST.new"
mv -f "$LIST.new" "$LIST"

echo "Installing Kobalt in $APP"
# The package is unpacked on the same storage, so mv renames each file instead
# of writing a second copy of the app.
while IFS= read -r file; do
    directory=$(dirname "$APP/$file")
    [ -d "$directory" ] || mkdir -p "$directory"
    mv -f "kobalt/$file" "$APP/$file"
done <kobalt-files.txt

# Remove files that only the previous package installed, and any directories
# they leave empty. rmdir stops at the first directory that still has files.
grep -Fxv -f kobalt-files.txt "$LIST" | while IFS= read -r file; do
    case "$file" in ''|/*|../*|*/../*|data/*|logs/*) continue ;; esac
    rm -f "$APP/$file"
    (cd "$APP" && rmdir -p "$(dirname "$file")" 2>/dev/null) || :
done
cp kobalt-files.txt "$LIST.new"
mv -f "$LIST.new" "$LIST"

mkdir -p "$DOCUMENTS"
cp -f "scriptlets/$SCRIPTLET" "$DOCUMENTS/$SCRIPTLET"
echo "Kobalt installed. Open \"Kobalt\" in the Library to start it."
