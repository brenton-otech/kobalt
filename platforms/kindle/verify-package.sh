#!/usr/bin/env bash
set -euo pipefail

glibc_compatible() {
    [[ "$1" =~ ^[0-9]+(\.[0-9]+)+$ ]] || return 1
    awk -v version="$1" 'BEGIN {
        n = split(version, parts, ".");
        if (parts[1] < 2) exit 0;
        if (parts[1] > 2 || parts[2] > 20) exit 1;
        if (parts[2] < 20) exit 0;
        for (i = 3; i <= n; ++i) if (parts[i] > 0) exit 1;
        exit 0;
    }'
}

glibc_library() {
    local loader=${interpreter:-/lib/ld-linux-armhf.so.3}
    case "$1" in
        libc.so.6|libm.so.6|libdl.so.2|libpthread.so.0|librt.so.1|libutil.so.1|libresolv.so.2|libnsl.so.1|libanl.so.1|libcrypt.so.1) return 0 ;;
        "${loader##*/}") return 0 ;;
        *) return 1 ;;
    esac
}

fail() { echo "Kindle package verification failed: $*" >&2; exit 1; }

if [[ "${1:-}" == --self-test && $# == 1 ]]; then
    for version in 2.0 2.4 2.19 2.20 2.20.0; do
        glibc_compatible "$version" || fail "baseline parser rejected $version"
    done
    for version in 2.20.1 2.21 2.34 3.0 PRIVATE ABI_DT_RELR 2.invalid; do
        if glibc_compatible "$version"; then fail "baseline parser accepted $version"; fi
    done
    glibc_library libc.so.6 || fail 'libc whitelist'
    if glibc_library libatomic.so.1; then fail 'libatomic must be packaged'; fi
    for interpreter in /lib/ld-linux-armhf.so.3 /lib/ld-linux.so.3; do
        glibc_library "${interpreter##*/}" || fail 'selected loader missing from whitelist'
        other=ld-linux.so.3
        [[ "$interpreter" != /lib/ld-linux.so.3 ]] || other=ld-linux-armhf.so.3
        if glibc_library "$other"; then fail 'opposite loader accepted'; fi
    done
    echo 'Kindle package audit parser checks passed.'
    exit 0
fi

root=$(cd "$(dirname "$0")/../.." && pwd)
source "$root/platforms/kindle/target.sh"
if [[ "${1:-}" == --inside ]]; then
    [[ $# == 2 && "$root" == /work ]] || fail 'invalid inside invocation'
    kindle_target "$2"
else
    [[ $# -le 1 ]] || fail 'Usage: bash platforms/kindle/verify-package.sh [pw5|pw3|--self-test]'
    kindle_target "${1:-pw5}"
fi
export DOCKER_CONFIG=${DOCKER_CONFIG:-"$root/build/$fixture/docker-config"}
mkdir -p "$DOCKER_CONFIG"
if [[ "${1:-}" != --inside ]]; then
    [[ -f "$root/build/$output/package/kobalt/bin/kobalt" ]] || fail 'build/package the selected ARM app first'
    image=$(bash "$root/platforms/kindle/sdk/prepare-runtime-image.sh")
    exec docker run --rm --pull=never --init \
        -e "KOBALT_KINDLE_BIBLE_CHECK=${KOBALT_KINDLE_BIBLE_CHECK:-}" \
        --label kobalt.purpose=kindle-userspace-check \
        -v "$root:/work" -v "$root/build/$fixture:/fixture" \
        --workdir /work "$image" bash /work/platforms/kindle/verify-package.sh --inside "$target"
fi
build=/work/build/$output
exec 9>"$build/application.lock"
flock -n 9 || fail 'the Kindle output is already in use'
exec 8>/fixture/firmware-runtime.lock
flock -n 8 || fail 'the stock runtime fixture is being prepared'
app=$build/package/kobalt
sysroot=/fixture/firmware-runtime
readelf=/usr/bin/readelf
[[ -x "$readelf" ]] || fail 'SDK readelf missing'
[[ -f "$sysroot/firmware.sha256" && $(cat "$sysroot/firmware.sha256") == "$firmware_sha" ]] \
    || fail 'prepare the stock runtime first: bash platforms/kindle/sdk/prepare-runtime.sh'
(cd "$sysroot" && sha256sum --check --status files.sha256) || fail 'stock runtime checksum mismatch'
[[ $(cat "$app/launcher/runtime-target") == "$target" ]] || fail 'packaged runtime target mismatch'
sh "$app/launcher/check-runtime.sh" "$app" "$sysroot" || fail 'runtime preflight rejected the stock fixture'
for binary in "$app/bin/kobalt" "$app/plugins/platforms/libqkindle.so"; do
    "$readelf" --file-header "$binary" >/dev/null || fail "missing or invalid ELF: $binary"
done
[[ -z "$(find "$app" -type l -print -quit)" ]] || fail 'package contains symlinks unsupported by USB storage'

count=0
while IFS= read -r -d '' binary; do
    if ! header=$("$readelf" --file-header "$binary" 2>/dev/null); then continue; fi
    count=$((count + 1))
    grep -Eq 'Class:[[:space:]]+ELF32' <<<"$header" || fail "not ELF32: $binary"
    grep -Eq 'Machine:[[:space:]]+ARM[[:space:]]*$' <<<"$header" || fail "not ARM: $binary"
    grep -Eq 'Data:.*little endian' <<<"$header" || fail "not little endian: $binary"
    grep -q "$abi ABI" <<<"$header" || fail "not $abi ABI: $binary"
    dynamic=$("$readelf" --dynamic --wide "$binary")
    while IFS= read -r needed; do
        [[ -n "$needed" ]] || continue
        if [[ -f "$app/lib/$needed" ]]; then continue; fi
        glibc_library "$needed" || fail "unpackaged dependency $needed required by $binary"
        if [[ ! -f "$sysroot/lib/$needed" && ! -f "$sysroot/usr/lib/$needed" ]]; then
            fail "glibc dependency $needed absent from target sysroot"
        fi
    done < <(sed -n 's/.*(NEEDED).*\[\([^]]*\)\].*/\1/p' <<<"$dynamic")
    program_headers=$("$readelf" --program-headers --wide "$binary")
    if grep -q 'Requesting program interpreter:' <<<"$program_headers"; then
        grep -Fq "[Requesting program interpreter: $interpreter]" <<<"$program_headers" \
            || fail "unexpected executable interpreter: $binary"
    fi
    versions=$("$readelf" --version-info --wide "$binary")
    while IFS= read -r version; do
        glibc_compatible "$version" || fail "GLIBC_$version exceeds target baseline 2.20: $binary"
    done < <(awk '{ for (i = 1; i < NF; ++i) if ($i == "Name:" && $(i + 1) ~ /^GLIBC_/) {
        version = $(i + 1); sub(/^GLIBC_/, "", version); print version;
    }}' <<<"$versions" | sort -u)
done < <(find "$app/bin" "$app/lib" "$app/plugins" "$build/sdk-platforms" -type f -print0)

program_headers=$("$readelf" --program-headers --wide "$app/bin/kobalt")
grep -Fq "[Requesting program interpreter: $interpreter]" <<<"$program_headers" \
    || fail 'unexpected executable interpreter'
echo "ARM ELF audit passed: $count objects, dependency closure, $abi ABI, GLIBC <= 2.20 ($target)."
bash /work/platforms/kindle/power-bridge-test.sh "$build/test-tmp"

# The SDK offscreen plugin uses the same FreeType database as the device QPA.
# qemu-user executes ARM userspace; it does not emulate a Kindle kernel or EPDC.
[[ -f "$build/sdk-platforms/libqoffscreen.so" ]] || fail 'matching SDK offscreen plugin missing; rebuild the application'
mkdir -p "$build/test-tmp"
profile=$(mktemp -d /tmp/arm-smoke.XXXXXX)
cleanup() { rm -rf "$profile"; }
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir -p "$profile/tmp" "$profile/runtime"
chmod 700 "$profile/runtime"
cp -r "$app/assets" "$profile/assets"
log=$build/arm-smoke.log
result=0
ulimit -c 0
timeout --signal=TERM --kill-after=10s 180s qemu-arm -cpu "$qemu_cpu" -L "$sysroot" \
    -E "LD_LIBRARY_PATH=$app/lib" \
    -E QT_QPA_PLATFORM=offscreen \
    -E KOBALT_STRICT_MAIN_THREAD=1 \
    -E "KOBALT_KINDLE_CHECK_CAPTURE_DIR=$build/folder-picker-captures" \
    -E "QT_QPA_PLATFORM_PLUGIN_PATH=$build/sdk-platforms" \
    -E "QT_PLUGIN_PATH=$app/plugins" \
    -E "QT_QPA_FONTDIR=$profile/assets/fonts" \
    -E "TMPDIR=$profile/tmp" \
    -E "XDG_RUNTIME_DIR=$profile/runtime" \
    "$app/bin/kobalt" --assets "$profile/assets" \
    --smoke-test --profile "$profile/data" --library /work/tests/fixtures \
    --book /work/tests/fixtures/trevelyans-type-tester.kepub.epub >"$log" 2>&1 || result=$?
cat "$log"
[[ "$result" == 0 ]] || fail "ARM userspace smoke exited $result; see $log"
grep -q 'Kindle smoke passed:' "$log" || fail 'ARM app exited without the smoke completion marker'
[[ -s "$profile/data/config/kobalt.sqlite" ]] || fail 'ARM smoke did not create its SQLite profile'
echo 'ARM userspace smoke passed. This check does not exercise framebuffer, input, suspend or stock UI.'

# Optional: the World English Bible fixture is large, so qemu takes longer.
if [[ "${KOBALT_KINDLE_BIBLE_CHECK:-}" == 1 ]]; then
    rm -rf "$profile/data" "$profile/tmp"
    mkdir -p "$profile/tmp"
    log=$build/arm-bible.log
    result=0
    timeout --signal=TERM --kill-after=10s 600s qemu-arm -cpu "$qemu_cpu" -L "$sysroot" \
        -E "LD_LIBRARY_PATH=$app/lib" \
        -E QT_QPA_PLATFORM=offscreen \
        -E KOBALT_STRICT_MAIN_THREAD=1 \
        -E "KOBALT_KINDLE_CHECK_CAPTURE_DIR=$build/bible-captures" \
        -E "QT_QPA_PLATFORM_PLUGIN_PATH=$build/sdk-platforms" \
        -E "QT_PLUGIN_PATH=$app/plugins" \
        -E "QT_QPA_FONTDIR=$profile/assets/fonts" \
        -E "TMPDIR=$profile/tmp" \
        -E "XDG_RUNTIME_DIR=$profile/runtime" \
        "$app/bin/kobalt" --assets "$profile/assets" \
        --smoke-test --bible-check --profile "$profile/data" --library /work/tests/fixtures \
        --book /work/tests/fixtures/web-bible.epub >"$log" 2>&1 || result=$?
    cat "$log"
    [[ "$result" == 0 ]] || fail "ARM Bible smoke exited $result; see $log"
    grep -q 'Kindle Bible picker opened' "$log" || fail 'ARM Bible smoke did not reach John 3'
    grep -q 'Kindle smoke passed:' "$log" || fail 'ARM Bible smoke exited without the completion marker'
    echo 'ARM Bible smoke passed: picker navigation, reference, page turn and restored position.'
fi
