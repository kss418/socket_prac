#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR_RAW="${BUILD_DIR:-build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_JOBS="${BUILD_JOBS:-}"
BUILD_DIR="${ROOT_DIR}/${BUILD_DIR_RAW}"
OUTPUT_DIR_RAW="${OUTPUT_DIR:-.}"

resolve_path_from_root() {
    local p="$1"
    if [[ "${p}" = /* ]]; then
        echo "${p}"
    else
        echo "${ROOT_DIR}/${p}"
    fi
}

OUTPUT_DIR="$(resolve_path_from_root "${OUTPUT_DIR_RAW}")"

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

mkdir -p "${BUILD_DIR}" "${OUTPUT_DIR}"

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

info "building server/client targets (jobs=${BUILD_JOBS})"
cmake --build "${BUILD_DIR}" --target server client -j "${BUILD_JOBS}"

if [[ ! -f "${BUILD_DIR}/server" ]]; then
    fail "build output not found: ${BUILD_DIR}/server"
fi
if [[ ! -f "${BUILD_DIR}/client" ]]; then
    fail "build output not found: ${BUILD_DIR}/client"
fi

install -m 755 "${BUILD_DIR}/server" "${OUTPUT_DIR}/server"
install -m 755 "${BUILD_DIR}/client" "${OUTPUT_DIR}/client"

echo "[PASS] server/client build success"
echo "[INFO] server binary: ${OUTPUT_DIR}/server"
echo "[INFO] client binary: ${OUTPUT_DIR}/client"
