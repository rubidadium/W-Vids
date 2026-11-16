#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$project_dir/build"

# Extra arguments are passed to CMake (for example, -DCMAKE_PREFIX_PATH=/path/to/Qt).
cmake -S "$project_dir" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build "$build_dir" --parallel

executable="$build_dir/bin/w-vids"
if [[ -f "$executable.exe" ]]; then
    executable="$executable.exe"
fi
exec "$executable"
