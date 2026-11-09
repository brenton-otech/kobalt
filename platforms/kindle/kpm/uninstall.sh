#!/bin/sh

# KPM runs this hook with sh from the package folder before deleting it. On an
# upgrade ("upgrade") the next package's install hook replaces the app files,
# so keep them. A full uninstall removes only the files the package recorded
# and the scriptlet it installed. data/ (settings and progress) and logs/
# always stay.
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
[ "${1:-}" != upgrade ] || exit 0

if [ -f "$LIST" ]; then
    echo "Removing Kobalt from $APP"
    while IFS= read -r file; do
        case "$file" in ''|/*|../*|*/../*|data/*|logs/*) continue ;; esac
        rm -f "$APP/$file"
        (cd "$APP" && rmdir -p "$(dirname "$file")" 2>/dev/null) || :
    done <"$LIST"
    rm -f "$LIST"
    rmdir "$APP" 2>/dev/null || echo "Kept $APP/data and $APP/logs."
fi
# Leave a scriptlet that the user changed.
if [ -f "$DOCUMENTS/$SCRIPTLET" ] && [ -f "scriptlets/$SCRIPTLET" ] &&
    cmp -s "scriptlets/$SCRIPTLET" "$DOCUMENTS/$SCRIPTLET"; then
    rm -f "$DOCUMENTS/$SCRIPTLET"
fi
