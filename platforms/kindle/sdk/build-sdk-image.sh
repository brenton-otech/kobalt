#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
image="localhost/kobuild:kindle"

echo "Building Kindle SDK image: $image..."
docker build --platform linux/amd64 -f "$root/platforms/kindle/sdk/Dockerfile.kindle" -t "$image" "$root"
echo "Kindle SDK image built successfully: $image"
