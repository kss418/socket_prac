#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SERVER_BIN="${ROOT_DIR}/build/server"
CLIENT_BIN="${ROOT_DIR}/build/client"
LOG_DIR="${ROOT_DIR}/log"

mkdir -p "${LOG_DIR}"
SERVER_LOG="$(mktemp "${LOG_DIR}/tls-server-normal-XXXX.log")"
CLIENT_LOG="$(mktemp "${LOG_DIR}/tls-client-normal-XXXX.log")"
SERVER_PID=""
SEARCH_BIN=""

if command -v rg >/dev/null 2>&1; then
    SEARCH_BIN="rg"
else
    SEARCH_BIN="grep"
fi

cleanup() {
    if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
        kill "${SERVER_PID}" 2>/dev/null || true
        wait "${SERVER_PID}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

fail() {
    local msg="$1"
    echo "[FAIL] ${msg}"
    echo "--- server log (${SERVER_LOG}) ---"
    cat "${SERVER_LOG}" || true
    echo "--- client log (${CLIENT_LOG}) ---"
    cat "${CLIENT_LOG}" || true
    exit 1
}

contains() {
    local pattern="$1"
    local file="$2"
    if [[ "${SEARCH_BIN}" == "rg" ]]; then
        rg -q "${pattern}" "${file}"
    else
        grep -q "${pattern}" "${file}"
    fi
}

[[ -x "${SERVER_BIN}" ]] || fail "server binary not found: ${SERVER_BIN}"
[[ -x "${CLIENT_BIN}" ]] || fail "client binary not found: ${CLIENT_BIN}"

echo "[INFO] starting server: ${SERVER_BIN}"
if command -v stdbuf >/dev/null 2>&1; then
    stdbuf -oL -eL "${SERVER_BIN}" >"${SERVER_LOG}" 2>&1 &
else
    "${SERVER_BIN}" >"${SERVER_LOG}" 2>&1 &
fi
SERVER_PID=$!

sleep 1
if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
    if contains "db connect failed" "${SERVER_LOG}"; then
        fail "server exited immediately (db connect failed; check DB or DB_PASSWORD)"
    fi
    fail "server exited immediately"
fi

echo "[INFO] running client and closing normally"
set +e
timeout 10s bash -lc "\"${CLIENT_BIN}\" < /dev/null" >"${CLIENT_LOG}" 2>&1
CLIENT_RC=$?
set -e

if [[ "${CLIENT_RC}" -ne 0 ]]; then
    fail "client exit code is ${CLIENT_RC} (expected 0)"
fi

sleep 1
if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
    fail "server is not alive after client exit"
fi

if ! contains "is connected" "${SERVER_LOG}"; then
    fail "server log missing 'is connected'"
fi

if ! contains "is disconnected" "${SERVER_LOG}"; then
    fail "server log missing 'is disconnected'"
fi

if contains "Protocol error" "${SERVER_LOG}"; then
    fail "server log contains 'Protocol error'"
fi

echo "[PASS] normal TLS connect/close test passed"
echo "[INFO] server log: ${SERVER_LOG}"
echo "[INFO] client log: ${CLIENT_LOG}"
