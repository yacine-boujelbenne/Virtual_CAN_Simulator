#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
if [[ ! -x build/native/vehicle_lab_desktop ]]; then
    printf '%s\n' 'Build the native desktop first; see native/DESKTOP.md.' >&2
    exit 1
fi
exec ./build/native/vehicle_lab_desktop --root "$root" "$@"
