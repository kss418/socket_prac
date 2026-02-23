#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CONFIG_FILE="${TEST_CONFIG:-${ROOT_DIR}/config/test_tls.conf}"
source "${ROOT_DIR}/scripts/lib/common.sh"

SERVER_BIN="$(resolve_path_from_root "$(cfg_get "test.server_bin" "build/server")")"
CLIENT_BIN="$(resolve_path_from_root "$(cfg_get "test.client_bin" "build/client")")"
LOG_DIR="$(resolve_path_from_root "$(cfg_get "test.log_dir" "test_log")")"
CLIENT_IP="$(cfg_get "test.client_ip" "127.0.0.1")"
CLIENT_PORT="$(cfg_get "test.client_port" "8080")"
SERVER_BOOT_WAIT_SEC="$(cfg_get "test.server_boot_wait_sec" "1")"
POST_CHECK_WAIT_SEC="$(cfg_get "test.post_check_wait_sec" "1")"
CLIENT_TIMEOUT_SEC="$(cfg_get "test.plaintext.client_timeout_sec" "10")"
ATTACK_TIMEOUT_SEC="$(cfg_get "test.plaintext.attack_timeout_sec" "2")"
PLAINTEXT_PAYLOAD="$(cfg_get "test.plaintext.payload" "HELLO_PLAINTEXT")"
ENV_FILE="$(resolve_path_from_root "$(cfg_get "test.env_file" ".env")")"
load_env_file "${ENV_FILE}"

mkdir -p "${LOG_DIR}"
SERVER_LOG="$(make_timestamped_path "${LOG_DIR}" "tls-server-plaintext" "log")"
ATTACK_LOG="$(make_timestamped_path "${LOG_DIR}" "tls-plaintext-attack" "log")"
CLIENT_LOG="$(make_timestamped_path "${LOG_DIR}" "tls-client-after-plaintext" "log")"

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

fail() {
    local msg="$1"
    echo "[FAIL] ${msg}"
    echo "--- server log (${SERVER_LOG}) ---"
    cat "${SERVER_LOG}" || true
    echo "--- attack log (${ATTACK_LOG}) ---"
    cat "${ATTACK_LOG}" || true
    echo "--- client log (${CLIENT_LOG}) ---"
    cat "${CLIENT_LOG}" || true
    exit 1
}

send_plaintext() {
    : > "${ATTACK_LOG}"

    if command -v nc >/dev/null 2>&1; then
        set +e
        timeout "${ATTACK_TIMEOUT_SEC}s" \
            bash -lc "printf '%s\n' \"\$1\" | nc \"\$2\" \"\$3\"" _ \
            "${PLAINTEXT_PAYLOAD}" "${CLIENT_IP}" "${CLIENT_PORT}" \
            >>"${ATTACK_LOG}" 2>&1
        ATTACK_RC=$?
        set -e
    else
        set +e
        timeout "${ATTACK_TIMEOUT_SEC}s" \
            bash -lc 'exec 3<>/dev/tcp/"$1"/"$2"; printf "%s\n" "$3" >&3; exec 3>&-; exec 3<&-' _ \
            "${CLIENT_IP}" "${CLIENT_PORT}" "${PLAINTEXT_PAYLOAD}" \
            >>"${ATTACK_LOG}" 2>&1
        ATTACK_RC=$?
        set -e
    fi

    if [[ "${ATTACK_RC}" -eq 124 ]]; then
        fail "plaintext attack connection timed out"
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

sleep "${SERVER_BOOT_WAIT_SEC}"
if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
    if contains "db connect failed" "${SERVER_LOG}"; then
        fail "server exited immediately (db connect failed; check db.password in .env)"
    fi
    fail "server exited immediately"
fi

echo "[INFO] sending plaintext payload to TLS port"
send_plaintext

sleep "${POST_CHECK_WAIT_SEC}"
if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
    fail "server crashed after plaintext payload"
fi

echo "[INFO] running normal TLS client after plaintext attempt"
set +e
timeout "${CLIENT_TIMEOUT_SEC}s" \
    "${CLIENT_BIN}" "${CLIENT_IP}" "${CLIENT_PORT}" \
    < /dev/null >"${CLIENT_LOG}" 2>&1
CLIENT_RC=$?
set -e

if [[ "${CLIENT_RC}" -ne 0 ]]; then
    fail "TLS client failed after plaintext attempt (rc=${CLIENT_RC})"
fi

sleep "${POST_CHECK_WAIT_SEC}"
if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
    fail "server crashed after TLS client reconnect"
fi

echo "[PASS] plaintext rejection test passed"
echo "[INFO] server log: ${SERVER_LOG}"
echo "[INFO] attack log: ${ATTACK_LOG}"
echo "[INFO] client log: ${CLIENT_LOG}"
