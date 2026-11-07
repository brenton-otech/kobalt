#!/usr/bin/env bash
set -euo pipefail
if [[ $# -gt 1 ]]; then echo "Usage: bash platforms/kindle/build.sh [pw5|pw3|colorsoft]" >&2; exit 2; fi
root=$(cd "$(dirname "$0")/../.." && pwd)
source "$root/platforms/kindle/target.sh"
kindle_target "${1:-pw5}"
export DOCKER_CONFIG=${DOCKER_CONFIG:-"$root/build/$fixture/docker-config"}
mkdir -p "$DOCKER_CONFIG"
image=${KOBALT_KINDLE_SDK_IMAGE:-$sdk_image}
jobs=${JOBS:-4}
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo 'JOBS must be positive' >&2; exit 2; }
sdk_id=$(docker image inspect "$image" --format '{{.Id}}' 2>/dev/null) || {
    echo "Prepare the Kindle SDK image in kobuild first: ./build.sh $preset ($image)" >&2; exit 1;
}
git_command=(git -C "$root")
# A worktree created by Windows Git records a Windows absolute gitdir.
# Use that Git through WSL interop rather than rewriting the user's worktree.
if [[ -f "$root/.git" ]] && grep -Eq '^gitdir: [A-Za-z]:[/\\]' "$root/.git" &&
    command -v git.exe >/dev/null && command -v wslpath >/dev/null; then
    git_command=(git.exe -C "$(wslpath -w "$root")")
fi
commit=$("${git_command[@]}" rev-parse --short HEAD)
modified=OFF
if [[ -n $("${git_command[@]}" status --porcelain) ]]; then modified=ON; fi
docker run --rm --pull=never --platform linux/amd64 --init \
    --label kobalt.purpose=kindle-app \
    -e JOBS="$jobs" -e KOBALT_KINDLE_QT=/sdk/qt -e KOBALT_KINDLE_QT_HOST=/sdk/qt-host \
    -e KOBALT_KINDLE_PRESET="$preset" -e KOBALT_KINDLE_OUTPUT="$output" \
    -e KOBALT_KINDLE_SDK_ID="$sdk_id" \
    -e KOBALT_SOURCE_COMMIT="$commit" -e KOBALT_SOURCE_MODIFIED="$modified" \
    -v "$root:/work" \
    --workdir /work/platforms/kindle "$image" bash -euc '
        build=/work/build/$KOBALT_KINDLE_OUTPUT
        mkdir -p "$build"
        exec 9>"$build/application.lock"
        flock -n 9 || { echo "Kindle output is already in use" >&2; exit 1; }
        cmake --preset "$KOBALT_KINDLE_PRESET" -DKOBALT_DEPENDENCY_JOBS="$JOBS" \
            -DKOBALT_SDK_ID="$KOBALT_KINDLE_SDK_ID" \
            -DKOBALT_COMMIT="$KOBALT_SOURCE_COMMIT" -DKOBALT_COMMIT_MODIFIED="$KOBALT_SOURCE_MODIFIED"
        cmake --build --preset "$KOBALT_KINDLE_PRESET" --parallel "$JOBS"
        cmake --install "$build"
        cmake -DBUILD="$build" -DSDK=/sdk -P package.cmake
        mkdir -p "$build/sdk-platforms"
        cp /sdk/qt/plugins/platforms/libqoffscreen.so "$build/sdk-platforms/"
    '
