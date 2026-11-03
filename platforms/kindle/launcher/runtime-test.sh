#!/bin/sh
set -eu
SCRIPT=$(CDPATH='' cd "$(dirname "$0")" && pwd -P)/check-runtime.sh
mkdir -p "${1:?pass an ignored scratch directory}"
scratch=$(mktemp -d "$1/runtime-test.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
mkdir -p "$scratch/app/launcher" "$scratch/system/lib"
printf 'pw3\n' >"$scratch/app/launcher/runtime-target"
touch "$scratch/system/lib/ld-linux.so.3"
sh "$SCRIPT" "$scratch/app" "$scratch/system"
printf 'pw5\n' >"$scratch/app/launcher/runtime-target"
if sh "$SCRIPT" "$scratch/app" "$scratch/system"; then
    echo 'Hard-float package accepted a softfp-only system.' >&2; exit 1
fi
rm "$scratch/system/lib/ld-linux.so.3"
touch "$scratch/system/lib/ld-linux-armhf.so.3"
sh "$SCRIPT" "$scratch/app" "$scratch/system"
printf 'pw3\n' >"$scratch/app/launcher/runtime-target"
if sh "$SCRIPT" "$scratch/app" "$scratch/system"; then
    echo 'PW3 package accepted a hard-float-only system.' >&2; exit 1
fi
printf 'unknown\n' >"$scratch/app/launcher/runtime-target"
if sh "$SCRIPT" "$scratch/app" "$scratch/system"; then
    echo 'Unknown target accepted.' >&2; exit 1
fi
echo 'Kindle runtime preflight checks passed.'
