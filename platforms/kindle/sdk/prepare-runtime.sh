#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
source "$root/platforms/kindle/target.sh"
if [[ "${1:-}" == --inside ]]; then
    [[ $# == 2 && "$root" == /work ]] || { echo 'Invalid inside invocation.' >&2; exit 2; }
    kindle_target "$2"
else
    [[ $# -le 1 ]] || { echo 'Usage: bash platforms/kindle/sdk/prepare-runtime.sh [pw5|pw3|colorsoft]' >&2; exit 2; }
    kindle_target "${1:-pw5}"
    export DOCKER_CONFIG=${DOCKER_CONFIG:-"$root/build/$fixture/docker-config"}
    mkdir -p "$DOCKER_CONFIG" "$root/build/$fixture"
    image=$(bash "$root/platforms/kindle/sdk/prepare-runtime-image.sh")
    exec docker run --rm --pull=never --init --label kobalt.purpose=kindle-firmware-runtime \
        -v "$root:/work" -v "$root/build/$fixture:/sdk" --workdir /work \
        "$image" bash /work/platforms/kindle/sdk/prepare-runtime.sh --inside "$target"
fi

exec 9>/sdk/firmware-runtime.lock
flock -n 9 || { echo 'The firmware runtime fixture is already being prepared.' >&2; exit 1; }
downloads=/sdk/downloads
runtime=/sdk/firmware-runtime
mkdir -p "$downloads"
tool_commit=95291295e039a1a3bc0bc9d6040b07f640dae74a
tool_archive=KindleTool-$tool_commit.tar.gz
tool_sha=011ae7463000bc3bb602321afa9bb13094dce25f43ca7621f6277b0c2a649e87

fetch() {
    local name=$1 url=$2 digest=$3
    if [[ ! -f "$downloads/$name" ]]; then
        curl --fail --location --retry 3 --output "$downloads/$name.partial" "$url"
        printf '%s  %s\n' "$digest" "$downloads/$name.partial" | sha256sum --check --status
        mv "$downloads/$name.partial" "$downloads/$name"
    fi
    printf '%s  %s\n' "$digest" "$downloads/$name" | sha256sum --check --status
}
fetch "$firmware" "https://s3.amazonaws.com/firmwaredownloads/$firmware" "$firmware_sha"
[[ $(stat -c %s "$downloads/$firmware") == "$firmware_size" ]] || { echo 'Unexpected firmware size.' >&2; exit 1; }

verify_runtime() {
    local candidate=${1:-$runtime}
    local loader=$candidate$interpreter
    [[ -L "$loader" ]] && loader=$(dirname "$loader")/$(readlink "$loader")
    [[ -f "$candidate/firmware.sha256" && $(cat "$candidate/firmware.sha256") == "$firmware_sha" &&
       -f "$candidate/files.sha256" && "$loader" -ef "$candidate/$runtime_loader" ]] || return 1
    readelf -h "$candidate/$runtime_libc" | grep -q "$abi ABI" || return 1
    (
        cd "$candidate"
        printf '%s  %s\n%s  %s\n' "$loader_sha" "$runtime_loader" "$libc_sha" "$runtime_libc" |
            sha256sum --check --status || exit 1
        sha256sum --check --status files.sha256
    )
}
if verify_runtime; then
    echo "Verified cached $target runtime from $firmware (stock glibc $runtime_glibc)."
    exit 0
fi

fetch "$tool_archive" "https://codeload.github.com/NiLuJe/KindleTool/tar.gz/$tool_commit" "$tool_sha"
sources=/sdk/src/firmware-tools/$(uname -m)
mkdir -p "$sources"
tool=$sources/KindleTool-$tool_commit/KindleTool/Release/kindletool
if [[ ! -x "$tool" ]]; then
    tar -xzf "$downloads/$tool_archive" -C "$sources"
    make -C "$sources/KindleTool-$tool_commit/KindleTool" -j2
fi

scratch=$(mktemp -d /sdk/firmware-runtime.prepare.XXXXXX)
cleanup() {
    if [[ -f "$scratch/extract.log" ]]; then
        cp "$scratch/extract.log" /sdk/firmware-runtime-extract.log
    fi
    rm -rf "$scratch"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir -p "$scratch/unpacked" "$scratch/runtime/usr"
"$tool" extract "$downloads/$firmware" "$scratch/unpacked" >"$scratch/extract.log" 2>&1
gzip -dc "$scratch/unpacked/rootfs.img.gz" >"$scratch/rootfs.img"
# debugfs opens the image read-only without -w. No loop device or mount is used.
debugfs -R "rdump /lib $scratch/runtime" "$scratch/rootfs.img"
debugfs -R "rdump /usr/lib $scratch/runtime/usr" "$scratch/rootfs.img"
debugfs -R 'cat /etc/version.txt' "$scratch/rootfs.img" >"$scratch/runtime/version.txt"
grep -Fq "$firmware_release" "$scratch/runtime/version.txt"
printf '%s\n' "$firmware_sha" >"$scratch/runtime/firmware.sha256"
(
    cd "$scratch/runtime"
    find lib usr/lib -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum >files.sha256
)
verify_runtime "$scratch/runtime"
rm -rf "$runtime"
mv "$scratch/runtime" "$runtime"
verify_runtime
echo "Prepared $target runtime from $firmware (stock glibc $runtime_glibc)."
