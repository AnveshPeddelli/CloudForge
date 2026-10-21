#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_root"
if [[ ! -x build/demo/cloudforge_viewer ]]; then
    echo "Build the viewer first. See README.md (CUDA desktop build)." >&2
    exit 1
fi
exec ./build/demo/cloudforge_viewer "$@"
