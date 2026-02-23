#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CONFIG_FILE="${TEST_CONFIG:-${ROOT_DIR}/config/test_tls.conf}"
source "${ROOT_DIR}/scripts/test/test_tls_common.sh"

SERVER_BIN="$(resolve_path_from_root "$(cfg_get "test.server_bin" "build/server")")"
CLIENT_BIN="$(resolve_path_from_root "$(cfg_get "test.client_bin" "build/client")")"
LOG_DIR="$(resolve_path_from_root "$(cfg_get "test.log_dir" "test_log")")"
CLIENT_IP="$(cfg_get "test.client_ip" "127.0.0.1")"
CLIENT_PORT="$(cfg_get "test.client_port" "8080")"
SERVER_BOOT_WAIT_SEC="$(cfg_get "test.server_boot_wait_sec" "1")"
POST_CHECK_WAIT_SEC="$(cfg_get "test.post_check_wait_sec" "1")"
CLIENT_TIMEOUT_SEC="$(cfg_get "test.stress.client_timeout_sec" "10")"
ITERATIONS="$(cfg_get "test.stress.iterations" "30")"
INTER_ITER_SLEEP_SEC="$(cfg_get "test.stress.inter_iteration_sleep_sec" "0")"
ENV_FILE="$(resolve_path_from_root "$(cfg_get "test.env_file" ".env")")"
load_env_file "${ENV_FILE}"

mkdir -p "${LOG_DIR}"
SERVER_LOG="$(make_timestamped_path "${LOG_DIR}" "tls-server-stress" "log")"
CLIENT_LOG="$(make_timestamped_path "${LOG_DIR}" "tls-client-stress" "log")"

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
    echo "--- client log (${CLIENT_LOG}) ---"
    cat "${CLIENT_LOG}" || true
    exit 1
}

[[ -x "${SERVER_BIN}" ]] || fail "server binary not found: ${SERVER_BIN}"
[[ -x "${CLIENT_BIN}" ]] || fail "client binary not found: ${CLIENT_BIN}"
[[ "${ITERATIONS}" =~ ^[0-9]+$ ]] || fail "test.stress.iterations must be numeric"
[[ "${ITERATIONS}" -gt 0 ]] || fail "test.stress.iterations must be > 0"

echo "[INFO] starting server: ${SERVER_BIN}"
if command -v stdbuf >/dev/null 2>&1; then
    stdbuf -oL -eL "${SERVER_BIN}" >"${SERVER_LOG}" 2>&1 &
else
    "${SERVER_BIN}" >"${SERVER_LOG}" 2>&1 &
fi
SERVER_PID=$!

sleep "${SERVER_BOOT_WAIT_SEC}"
if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
    if contains "db connect failed" "${SERVER_LOG}"; then
        fail "server exited immediately (db connect failed; check db.password in .env)"
    fi
    fail "server exited immediately"
fi

: > "${CLIENT_LOG}"
echo "[INFO] running reconnect stress (${ITERATIONS} iterations)"
for ((i=1; i<=ITERATIONS; ++i)); do
    echo "[INFO] iteration ${i}/${ITERATIONS}"
    {
        echo "[ITER ${i}]"
        date '+%Y-%m-%d %H:%M:%S'
    } >> "${CLIENT_LOG}"

    set +e
    timeout "${CLIENT_TIMEOUT_SEC}s" \
        "${CLIENT_BIN}" "${CLIENT_IP}" "${CLIENT_PORT}" \
        < /dev/null >>"${CLIENT_LOG}" 2>&1
    CLIENT_RC=$?
    set -e

    if [[ "${CLIENT_RC}" -ne 0 ]]; then
        fail "client failed at iteration ${i} (rc=${CLIENT_RC})"
    fi

    if [[ "${INTER_ITER_SLEEP_SEC}" != "0" ]]; then
        sleep "${INTER_ITER_SLEEP_SEC}"
    fi
done

sleep "${POST_CHECK_WAIT_SEC}"
if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
    fail "server crashed during reconnect stress"
fi

CONNECTED_COUNT="$(count_matches "is connected" "${SERVER_LOG}")"
if [[ "${CONNECTED_COUNT}" -lt "${ITERATIONS}" ]]; then
    fail "connected count is too low (want >= ${ITERATIONS}, got ${CONNECTED_COUNT})"
fi

DISCONNECTED_COUNT="$(count_matches "is disconnected" "${SERVER_LOG}")"
if [[ "${DISCONNECTED_COUNT}" -lt "${ITERATIONS}" ]]; then
    fail "disconnected count is too low (want >= ${ITERATIONS}, got ${DISCONNECTED_COUNT})"
fi

if contains "Protocol error" "${SERVER_LOG}"; then
    fail "server log contains 'Protocol error'"
fi

echo "[PASS] reconnect stress test passed"
echo "[INFO] iterations: ${ITERATIONS}"
echo "[INFO] server log: ${SERVER_LOG}"
echo "[INFO] client log: ${CLIENT_LOG}"
