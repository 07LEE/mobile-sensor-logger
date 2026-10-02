#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_root/build/host-tools"

cmake -S "$repo_root/tools" -B "$build_dir" >&2
cmake --build "$build_dir" --parallel >&2
"$build_dir/replay_sampling" "$@"
