#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "$root/build/deps/packages"
cd "$root/build/deps/packages"
# Ubuntu package names; use a system Assimp development package on other hosts.
apt-get download libassimp-dev libassimp6 libdraco9 libminizip1t64 libpugixml1v5 libpugixml-dev
for package in ./*.deb; do dpkg-deb -x "$package" ..; done
