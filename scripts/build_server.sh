#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR_RAW="${BUILD_DIR:-build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_JOBS="${BUILD_JOBS:-}"
BUILD_DIR="${ROOT_DIR}/${BUILD_DIR_RAW}"

fail() {
    echo "[FAIL] $1"
    exit 1
}

info() {
    echo "[INFO] $1"
}

if [[ ! -f "${ROOT_DIR}/CMakeLists.txt" ]]; then
    fail "CMakeLists.txt not found at project root: ${ROOT_DIR}"
fi

if ! command -v cmake >/dev/null 2>&1; then
    fail "cmake command not found"
fi

if [[ -z "${BUILD_JOBS}" ]]; then
    if command -v nproc >/dev/null 2>&1; then
        BUILD_JOBS="$(nproc)"
    else
        BUILD_JOBS="4"
    fi
fi

mkdir -p "${BUILD_DIR}"

configure_cmd=(cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")

# Pick Ninja on first configure when available, unless user set CMAKE_GENERATOR.
if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    if [[ -n "${CMAKE_GENERATOR:-}" ]]; then
        configure_cmd+=(-G "${CMAKE_GENERATOR}")
    elif command -v ninja >/dev/null 2>&1; then
        configure_cmd+=(-G Ninja)
    fi
fi

info "configuring project (build_dir=${BUILD_DIR_RAW}, build_type=${BUILD_TYPE})"
"${configure_cmd[@]}"

info "building server target (jobs=${BUILD_JOBS})"
cmake --build "${BUILD_DIR}" --target server -j "${BUILD_JOBS}"

echo "[PASS] server build success"
echo "[INFO] binary: ${BUILD_DIR}/server"
