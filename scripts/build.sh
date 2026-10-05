#!/bin/sh

set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
dependency_dir="$project_dir/.antlr"
runtime_archive="$dependency_dir/antlr4-cpp-runtime-4.13.2-source.zip"
runtime_url="https://www.antlr.org/download/antlr4-cpp-runtime-4.13.2-source.zip"
runtime_sha256="0ed13668906e86dbc0dcddf30fdee68c10203dea4e83852b4edb810821bee3c4"

mkdir -p "$dependency_dir"

if [ ! -f "$runtime_archive" ]; then
    temporary_archive="$runtime_archive.tmp"
    trap 'rm -f "$temporary_archive"' EXIT HUP INT TERM
    curl --fail --location "$runtime_url" --output "$temporary_archive"
    printf '%s  %s\n' "$runtime_sha256" "$temporary_archive" | sha256sum --check --status
    mv "$temporary_archive" "$runtime_archive"
    trap - EXIT HUP INT TERM
fi

printf '%s  %s\n' "$runtime_sha256" "$runtime_archive" | sha256sum --check --status

# VERBOSE controls the test reporter; CMake treats even "false" as verbose.
unset VERBOSE
cmake -S "$project_dir" -B "$project_dir/target/build" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$project_dir/target/build" --parallel "${BUILD_JOBS:-2}"
