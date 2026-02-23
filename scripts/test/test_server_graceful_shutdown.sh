#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CONFIG_FILE="${TEST_CONFIG:-${ROOT_DIR}/config/test_tls.conf}"
source "${ROOT_DIR}/scripts/test/test_tls_common.sh"

SERVER_BIN="$(resolve_path_from_root "$(cfg_get "test.server_bin" "build/server")")"
LOG_DIR="$(resolve_path_from_root "$(cfg_get "test.log_dir" "test_log")")"
CLIENT_PORT="$(cfg_get "test.client_port" "8080")"
SERVER_BOOT_WAIT_SEC="$(cfg_get "test.server_boot_wait_sec" "1")"
START_WAIT_TRY="$(cfg_get "test.graceful.start_wait_try" "50")"
START_WAIT_INTERVAL_SEC="$(cfg_get "test.graceful.start_wait_interval_sec" "0.1")"
STOP_WAIT_TRY="$(cfg_get "test.graceful.stop_wait_try" "100")"
STOP_WAIT_INTERVAL_SEC="$(cfg_get "test.graceful.stop_wait_interval_sec" "0.1")"
PORT_FREE_WAIT_TRY="$(cfg_get "test.graceful.port_free_wait_try" "50")"
PORT_FREE_WAIT_INTERVAL_SEC="$(cfg_get "test.graceful.port_free_wait_interval_sec" "0.1")"
ENV_FILE="$(resolve_path_from_root "$(cfg_get "test.env_file" ".env")")"
load_env_file "${ENV_FILE}"

mkdir -p "${LOG_DIR}"
RUN_TS="$(timestamp_now)"
RUN_DIR="${LOG_DIR}/server-graceful-shutdown-${RUN_TS}"
mkdir -p "${RUN_DIR}"
SIGINT_LOG="${RUN_DIR}/server-sigint-${RUN_TS}.log"
SIGTERM_LOG="${RUN_DIR}/server-sigterm-${RUN_TS}.log"

SERVER_PID=""
SEARCH_BIN=""

if command -v rg >/dev/null 2>&1; then
    SEARCH_BIN="rg"
else
    SEARCH_BIN="grep"
fi

contains() {
    local pattern="$1"
    local file="$2"
    if [[ "${SEARCH_BIN}" == "rg" ]]; then
        rg -q "${pattern}" "${file}"
    else
        grep -q "${pattern}" "${file}"
    fi
}

port_in_use() {
    local port="$1"
    ss -ltn 2>/dev/null | awk -v p=":${port}" '
    $1 == "LISTEN" {
        if($4 ~ (p "$")) found = 1;
    }
    END {
        if(found) exit 0;
        exit 1;
    }'
}

wait_for_log() {
    local pattern="$1"
    local file="$2"
    local try_count="$3"
    local interval_sec="$4"
    local i=0
    while [[ "${i}" -lt "${try_count}" ]]; do
        if contains "${pattern}" "${file}"; then
            return 0
        fi
        sleep "${interval_sec}"
        i=$((i + 1))
    done
    return 1
}

wait_for_port_used() {
    local i=0
    while [[ "${i}" -lt "${START_WAIT_TRY}" ]]; do
        if port_in_use "${CLIENT_PORT}"; then
            return 0
        fi
        sleep "${START_WAIT_INTERVAL_SEC}"
        i=$((i + 1))
    done
    return 1
}

wait_for_port_free() {
    local i=0
    while [[ "${i}" -lt "${PORT_FREE_WAIT_TRY}" ]]; do
        if ! port_in_use "${CLIENT_PORT}"; then
            return 0
        fi
        sleep "${PORT_FREE_WAIT_INTERVAL_SEC}"
        i=$((i + 1))
    done
    return 1
}

cleanup() {
    if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
        kill "${SERVER_PID}" 2>/dev/null || true
        sleep 0.2
        if kill -0 "${SERVER_PID}" 2>/dev/null; then
            kill -9 "${SERVER_PID}" 2>/dev/null || true
        fi
        wait "${SERVER_PID}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

fail() {
    local msg="$1"
    local log_file="${2:-}"
    echo "[FAIL] ${msg}"
    if [[ -n "${log_file}" ]]; then
        echo "--- server log (${log_file}) ---"
        cat "${log_file}" || true
    fi
    echo "--- run dir (${RUN_DIR}) ---"
    exit 1
}

start_server() {
    local log_file="$1"
    : > "${log_file}"

    if command -v stdbuf >/dev/null 2>&1; then
        stdbuf -oL -eL "${SERVER_BIN}" >"${log_file}" 2>&1 &
    else
        "${SERVER_BIN}" >"${log_file}" 2>&1 &
    fi
    SERVER_PID=$!

    sleep "${SERVER_BOOT_WAIT_SEC}"
    if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
        fail "server exited immediately during startup" "${log_file}"
    fi

    if ! wait_for_port_used; then
        fail "server did not start listening on port ${CLIENT_PORT}" "${log_file}"
    fi

    if ! wait_for_log "server run start port:${CLIENT_PORT}" "${log_file}" "${START_WAIT_TRY}" "${START_WAIT_INTERVAL_SEC}"; then
        fail "missing run start log" "${log_file}"
    fi
}

stop_server_with_signal() {
    local sig="$1"
    local log_file="$2"
    local sig_name="SIG${sig}"
    local i=0
    local rc=0

    if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
        fail "server process already dead before ${sig_name}" "${log_file}"
    fi

    kill "-${sig}" "${SERVER_PID}"
    while [[ "${i}" -lt "${STOP_WAIT_TRY}" ]]; do
        if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
            break
        fi
        sleep "${STOP_WAIT_INTERVAL_SEC}"
        i=$((i + 1))
    done

    if kill -0 "${SERVER_PID}" 2>/dev/null; then
        kill -9 "${SERVER_PID}" 2>/dev/null || true
        wait "${SERVER_PID}" 2>/dev/null || true
        fail "server did not exit after ${sig_name}" "${log_file}"
    fi

    set +e
    wait "${SERVER_PID}"
    rc=$?
    set -e
    SERVER_PID=""

    if [[ "${rc}" -ne 0 ]]; then
        fail "server exited with non-zero code ${rc} after ${sig_name}" "${log_file}"
    fi

    if ! wait_for_port_free; then
        fail "port ${CLIENT_PORT} still in use after ${sig_name}" "${log_file}"
    fi

    if ! contains "shutdown signal received: ${sig_name}" "${log_file}"; then
        fail "missing shutdown signal log for ${sig_name}" "${log_file}"
    fi

    if contains "server run failed" "${log_file}"; then
        fail "server reported run failure during ${sig_name} shutdown" "${log_file}"
    fi
}

[[ -x "${SERVER_BIN}" ]] || fail "server binary not found: ${SERVER_BIN}"
command -v ss >/dev/null 2>&1 || fail "ss command not found"

if port_in_use "${CLIENT_PORT}"; then
    fail "port ${CLIENT_PORT} is already in use before test start"
fi

echo "[INFO] graceful shutdown test start (port=${CLIENT_PORT})"

echo "[INFO] case 1/2: SIGINT"
start_server "${SIGINT_LOG}"
stop_server_with_signal "INT" "${SIGINT_LOG}"
echo "[PASS] SIGINT graceful shutdown"

echo "[INFO] case 2/2: SIGTERM"
start_server "${SIGTERM_LOG}"
stop_server_with_signal "TERM" "${SIGTERM_LOG}"
echo "[PASS] SIGTERM graceful shutdown"

echo "[PASS] server graceful shutdown test passed"
echo "[INFO] run dir: ${RUN_DIR}"
echo "[INFO] sigint log: ${SIGINT_LOG}"
echo "[INFO] sigterm log: ${SIGTERM_LOG}"
