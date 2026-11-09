#!/bin/sh
set -eu
APP_DIR=${1:?pass the application directory}
SYSTEM_ROOT=${2:-/}
target=$(cat "$APP_DIR/launcher/runtime-target") || exit 1
case "$target" in
    pw5|colorsoft) loader=ld-linux-armhf.so.3 ;;
    pw3) loader=ld-linux.so.3 ;;
    *) echo "Unsupported Kobalt runtime target: $target" >&2; exit 1 ;;
esac
if [ ! -e "$SYSTEM_ROOT/lib/$loader" ]; then
    echo "Kobalt $target requires /lib/$loader; install the package matching this firmware ABI." >&2
    exit 1
fi
# Hard-float firmware such as Colorsoft 5.20 links ld-linux.so.3 to its
# hard-float loader. A softfp package must not start there.
if [ "$target" = pw3 ] && [ -e "$SYSTEM_ROOT/lib/ld-linux-armhf.so.3" ]; then
    echo "Kobalt pw3 needs softfp firmware; this system uses the hard-float loader." >&2
    exit 1
fi
