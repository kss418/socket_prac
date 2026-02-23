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
CLIENT_TIMEOUT_SEC="$(cfg_get "test.mismatch.client_timeout_sec" "10")"
MISMATCH_CA_DAYS="$(cfg_get "test.mismatch.ca_days" "1")"
MISMATCH_CA_SUBJECT="$(cfg_get "test.mismatch.ca_subject" "/CN=socket-prac-mismatch-ca")"
ENV_FILE="$(resolve_path_from_root "$(cfg_get "test.env_file" ".env")")"
load_env_file "${ENV_FILE}"

mkdir -p "${LOG_DIR}"
SERVER_LOG="$(make_timestamped_path "${LOG_DIR}" "tls-server-mismatch" "log")"
CLIENT_LOG="$(make_timestamped_path "${LOG_DIR}" "tls-client-mismatch" "log")"
MISMATCH_CA_CERT="$(mktemp "${LOG_DIR}/tls-mismatch-ca-XXXX.crt.pem")"
MISMATCH_CA_KEY="$(mktemp "${LOG_DIR}/tls-mismatch-ca-XXXX.key.pem")"

SERVER_PID=""

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

has_any_tls_classified_error() {
    local file="$1"
    if contains "tls.verify_failed" "${file}"; then return 0; fi
    if contains "tls.verify_hostname_mismatch" "${file}"; then return 0; fi
    if contains "tls.handshake_failed" "${file}"; then return 0; fi
    if contains "tls.alert_received" "${file}"; then return 0; fi
    if contains "tls.protocol_error" "${file}"; then return 0; fi
    return 1
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
command -v openssl >/dev/null 2>&1 || fail "openssl command not found"

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

echo "[INFO] generating mismatch CA"
openssl req -x509 -newkey rsa:2048 -sha256 -days "${MISMATCH_CA_DAYS}" -nodes \
    -subj "${MISMATCH_CA_SUBJECT}" \
    -keyout "${MISMATCH_CA_KEY}" \
    -out "${MISMATCH_CA_CERT}" >/dev/null 2>&1

echo "[INFO] running client with mismatch CA"
set +e
timeout "${CLIENT_TIMEOUT_SEC}s" \
    "${CLIENT_BIN}" "${CLIENT_IP}" "${CLIENT_PORT}" "${MISMATCH_CA_CERT}" \
    < /dev/null >"${CLIENT_LOG}" 2>&1
CLIENT_RC=$?
set -e

if [[ "${CLIENT_RC}" -eq 0 ]]; then
    fail "client succeeded unexpectedly with mismatch CA"
fi

if [[ "${CLIENT_RC}" -eq 124 ]]; then
    fail "client timed out with mismatch CA"
fi

if ! has_any_tls_classified_error "${CLIENT_LOG}"; then
    fail "client log missing TLS classified error (expected tls.verify_*/tls.handshake_failed/tls.alert_received)"
fi

sleep "${POST_CHECK_WAIT_SEC}"
if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
    fail "server crashed after mismatch CA client attempt"
fi

echo "[PASS] certificate mismatch test passed"
echo "[INFO] client exit code: ${CLIENT_RC}"
echo "[INFO] server log: ${SERVER_LOG}"
echo "[INFO] client log: ${CLIENT_LOG}"
