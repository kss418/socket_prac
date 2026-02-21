#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SERVER_BIN="${ROOT_DIR}/build/server"
CLIENT_BIN="${ROOT_DIR}/build/client"
LOG_DIR="${ROOT_DIR}/log"

mkdir -p "${LOG_DIR}"
SERVER_LOG="$(mktemp "${LOG_DIR}/tls-server-forced-XXXX.log")"
CLIENT1_LOG="$(mktemp "${LOG_DIR}/tls-client1-forced-XXXX.log")"
CLIENT2_LOG="$(mktemp "${LOG_DIR}/tls-client2-forced-XXXX.log")"
CLIENT1_STDIN_FIFO="$(mktemp -u "${LOG_DIR}/tls-client1-stdin-XXXX.fifo")"

SERVER_PID=""
CLIENT1_PID=""
CLIENT1_WRITER_PID=""

if command -v rg >/dev/null 2>&1; then
    SEARCH_BIN="rg"
else
    SEARCH_BIN="grep"
fi

cleanup() {
    if [[ -n "${CLIENT1_PID}" ]] && kill -0 "${CLIENT1_PID}" 2>/dev/null; then
        kill "${CLIENT1_PID}" 2>/dev/null || true
        wait "${CLIENT1_PID}" 2>/dev/null || true
    fi

    if [[ -n "${CLIENT1_WRITER_PID}" ]] && kill -0 "${CLIENT1_WRITER_PID}" 2>/dev/null; then
        kill "${CLIENT1_WRITER_PID}" 2>/dev/null || true
        wait "${CLIENT1_WRITER_PID}" 2>/dev/null || true
    fi

    rm -f "${CLIENT1_STDIN_FIFO}" 2>/dev/null || true

    if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
        kill "${SERVER_PID}" 2>/dev/null || true
        wait "${SERVER_PID}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

contains() {
    local pattern="$1"
    local file="$2"
    if [[ "${SEARCH_BIN}" == "rg" ]]; then
        rg -q "${pattern}" "${file}"
    else
        grep -q "${pattern}" "${file}"
    fi
}

count_matches() {
    local pattern="$1"
    local file="$2"
    if [[ "${SEARCH_BIN}" == "rg" ]]; then
        rg -c "${pattern}" "${file}"
    else
        grep -c "${pattern}" "${file}"
    fi
}

fail() {
    local msg="$1"
    echo "[FAIL] ${msg}"
    echo "--- server log (${SERVER_LOG}) ---"
    cat "${SERVER_LOG}" || true
    echo "--- client1 log (${CLIENT1_LOG}) ---"
    cat "${CLIENT1_LOG}" || true
    echo "--- client2 log (${CLIENT2_LOG}) ---"
    cat "${CLIENT2_LOG}" || true
    exit 1
}

wait_for_log() {
    local pattern="$1"
    local file="$2"
    local max_try="${3:-30}"
    local i
    for ((i=0; i<max_try; ++i)); do
        if contains "${pattern}" "${file}"; then
            return 0
        fi
        sleep 0.2
    done
    return 1
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

echo "[INFO] running client #1"
mkfifo "${CLIENT1_STDIN_FIFO}"
(sleep 600 >"${CLIENT1_STDIN_FIFO}") &
CLIENT1_WRITER_PID=$!

set +e
"${CLIENT_BIN}" <"${CLIENT1_STDIN_FIFO}" >"${CLIENT1_LOG}" 2>&1 &
CLIENT1_PID=$!
set -e

if ! wait_for_log "is connected" "${SERVER_LOG}" 40; then
    fail "client #1 did not connect"
fi

echo "[INFO] kill -9 client #1 (${CLIENT1_PID})"
kill -9 "${CLIENT1_PID}" 2>/dev/null || true
wait "${CLIENT1_PID}" 2>/dev/null || true
CLIENT1_PID=""

sleep 1
if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
    fail "server crashed after client #1 forced termination"
fi

if ! wait_for_log "is disconnected" "${SERVER_LOG}" 40; then
    fail "server did not log disconnect after client #1 forced termination"
fi

echo "[INFO] running client #2 to verify reconnection"
set +e
timeout 10s bash -lc "\"${CLIENT_BIN}\" < /dev/null" >"${CLIENT2_LOG}" 2>&1
CLIENT2_RC=$?
set -e

if [[ "${CLIENT2_RC}" -ne 0 ]]; then
    fail "client #2 exit code is ${CLIENT2_RC} (expected 0)"
fi

sleep 1
if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
    fail "server crashed after client #2"
fi

CONNECTED_COUNT="$(count_matches "is connected" "${SERVER_LOG}")"
if [[ "${CONNECTED_COUNT}" -lt 2 ]]; then
    fail "expected at least 2 successful connections, got ${CONNECTED_COUNT}"
fi

if contains "Protocol error" "${SERVER_LOG}"; then
    fail "server log contains 'Protocol error'"
fi

echo "[PASS] forced termination test passed"
echo "[INFO] server log: ${SERVER_LOG}"
echo "[INFO] client1 log: ${CLIENT1_LOG}"
echo "[INFO] client2 log: ${CLIENT2_LOG}"
