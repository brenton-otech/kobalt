#!/usr/bin/env bash
set -euo pipefail
[[ $(uname -s) == Linux && $# == 1 ]] || { echo 'Run this check in the Linux verification container with an ignored scratch root.' >&2; exit 2; }
source_dir=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$1"
scratch=$(mktemp -d "$1/power-bridge.XXXXXX")
trap 'rm -rf "$scratch"' EXIT
trap 'exit 143' TERM
trap 'exit 130' INT
cc -std=c11 -Wall -Wextra -Werror -O2 "$source_dir/power_bridge.c" -ldl -o "$scratch/bridge"
cc -std=c11 -Wall -Wextra -Werror -shared -fPIC "$source_dir/power_bridge_test.c" -pthread -o "$scratch/liblipc.so.1"
export LD_LIBRARY_PATH="$scratch"
# The paused stock UI owns com.lab126.kaf. The bridge listens without
# claiming that name or publishing the framework's state.
{ sleep 0.2; } | timeout --kill-after=1s 3s env FAKE_DUPLICATE=1 "$scratch/bridge" >"$scratch/output" 2>"$scratch/errors"
grep -q '^anonymous-client$' "$scratch/errors"
if grep -q '^registered:' "$scratch/errors"; then
    echo 'Power bridge published framework state over the live framework.' >&2; exit 1
fi
[[ $(grep -c '^defer:2$' "$scratch/errors") == 2 ]] || { cat "$scratch/errors" >&2; echo 'Suspend deferral repeated within one sleep cycle.' >&2; exit 1; }
[[ $(grep -c '^readyToSuspend$' "$scratch/output") == 3 ]]
grep -q '^ready$' "$scratch/output"
grep -q '^closed$' "$scratch/errors"
# Firmware 5.13.5 times out when this live service connection polls blanket.
# Overlay ownership belongs to the supervisor; helper readiness must not wait
# for that query or depend on the screen-overlay service.
if ! { sleep 0.2; } | timeout --kill-after=1s 3s env FAKE_OVERLAY_TIMEOUT=1 "$scratch/bridge" >"$scratch/output" 2>"$scratch/errors"; then
    cat "$scratch/errors" >&2
    echo 'Screen-overlay query failure prevented power bridge readiness.' >&2
    exit 1
fi
grep -q '^ready$' "$scratch/output"
if grep -q 'overlay-query-timed-out' "$scratch/errors"; then
    echo 'Power bridge still polls the stock screen-overlay service.' >&2; exit 1
fi
# Without an anonymous client it falls back to a private service name, never kaf.
{ sleep 0.2; } | timeout --kill-after=1s 3s env FAKE_DUPLICATE=1 FAKE_NO_ANONYMOUS_CLIENT=1 "$scratch/bridge" >"$scratch/output" 2>"$scratch/errors" && result=0 || result=$?
[[ $result == 1 ]] && grep -q 'beside the framework: 17' "$scratch/errors" && ! grep -q '^ready$' "$scratch/output"
result=0
timeout --kill-after=1s 3s env FAKE_SUBSCRIBE_FAIL=1 "$scratch/bridge" </dev/null >"$scratch/output" 2>"$scratch/errors" || result=$?
[[ $result == 1 ]] && ! grep -q '^ready$' "$scratch/output" || { cat "$scratch/errors" >&2; exit 1; }
grep -q '^closed$' "$scratch/errors"
echo 'Power bridge callback delivery, bounded sleep deferral and startup failure checks passed.'
