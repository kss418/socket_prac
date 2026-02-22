#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG_FILE="${TEST_CONFIG:-${ROOT_DIR}/config/test_tls.conf}"
source "${ROOT_DIR}/scripts/test_tls_common.sh"

SERVER_BIN="$(resolve_path_from_root "$(cfg_get "test.server_bin" "build/server")")"
CLIENT_BIN="$(resolve_path_from_root "$(cfg_get "test.client_bin" "build/client")")"
LOG_DIR="$(resolve_path_from_root "$(cfg_get "test.log_dir" "test_log")")"
CLIENT_IP="$(cfg_get "test.client_ip" "127.0.0.1")"
CLIENT_PORT="$(cfg_get "test.client_port" "8080")"
SERVER_BOOT_WAIT_SEC="$(cfg_get "test.server_boot_wait_sec" "1")"
POST_CHECK_WAIT_SEC="$(cfg_get "test.post_check_wait_sec" "1")"
CLIENT_TIMEOUT_SEC="$(cfg_get "test.expired.client_timeout_sec" "10")"
SERVER_CONFIG="$(resolve_path_from_root "$(cfg_get "test.expired.server_config" "config/server.conf")")"
EXPIRED_CA_DAYS="$(cfg_get "test.expired.ca_days" "3650")"
EXPIRED_CA_SUBJECT="$(cfg_get "test.expired.ca_subject" "/CN=socket-prac-expired-ca")"
EXPIRED_SERVER_SUBJECT="$(cfg_get "test.expired.server_subject" "/CN=127.0.0.1")"
EXPIRED_NOT_BEFORE="$(cfg_get "test.expired.not_before" "20200101000000Z")"
EXPIRED_NOT_AFTER="$(cfg_get "test.expired.not_after" "20200102000000Z")"
ENV_FILE="$(resolve_path_from_root "$(cfg_get "test.env_file" ".env")")"
load_env_file "${ENV_FILE}"

mkdir -p "${LOG_DIR}"
SERVER_LOG="$(make_timestamped_path "${LOG_DIR}" "tls-server-expired" "log")"
CLIENT_LOG="$(make_timestamped_path "${LOG_DIR}" "tls-client-expired" "log")"
WORK_DIR="$(mktemp -d "${LOG_DIR}/tls-expired-artifacts-XXXX")"

SERVER_PID=""
ORIG_CERT_BACKUP=""
ORIG_KEY_BACKUP=""
TLS_CERT_PATH=""
TLS_KEY_PATH=""

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

    if [[ -n "${ORIG_CERT_BACKUP}" ]] && [[ -n "${TLS_CERT_PATH}" ]] && [[ -f "${ORIG_CERT_BACKUP}" ]]; then
        cp "${ORIG_CERT_BACKUP}" "${TLS_CERT_PATH}" 2>/dev/null || true
    fi

    if [[ -n "${ORIG_KEY_BACKUP}" ]] && [[ -n "${TLS_KEY_PATH}" ]] && [[ -f "${ORIG_KEY_BACKUP}" ]]; then
        cp "${ORIG_KEY_BACKUP}" "${TLS_KEY_PATH}" 2>/dev/null || true
    fi

    rm -rf "${WORK_DIR}" 2>/dev/null || true
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

has_any_expired_tls_error() {
    local file="$1"
    if contains "tls.verify_cert_expired" "${file}"; then return 0; fi
    if contains "tls.verify_failed" "${file}"; then return 0; fi
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
[[ -f "${SERVER_CONFIG}" ]] || fail "server config not found: ${SERVER_CONFIG}"
command -v openssl >/dev/null 2>&1 || fail "openssl command not found"

TLS_CERT_PATH="$(resolve_path_from_root "$(cfg_get_from_file "tls.cert" "" "${SERVER_CONFIG}")")"
TLS_KEY_PATH="$(resolve_path_from_root "$(cfg_get_from_file "tls.key" "" "${SERVER_CONFIG}")")"
[[ -n "${TLS_CERT_PATH}" ]] || fail "tls.cert is missing in ${SERVER_CONFIG}"
[[ -n "${TLS_KEY_PATH}" ]] || fail "tls.key is missing in ${SERVER_CONFIG}"
[[ -f "${TLS_CERT_PATH}" ]] || fail "server cert not found: ${TLS_CERT_PATH}"
[[ -f "${TLS_KEY_PATH}" ]] || fail "server key not found: ${TLS_KEY_PATH}"

EXPIRED_CA_CERT="${WORK_DIR}/expired-ca.crt.pem"
EXPIRED_CA_KEY="${WORK_DIR}/expired-ca.key.pem"
EXPIRED_SERVER_KEY="${WORK_DIR}/expired-server.key.pem"
EXPIRED_SERVER_CSR="${WORK_DIR}/expired-server.csr.pem"
EXPIRED_SERVER_CERT="${WORK_DIR}/expired-server.crt.pem"
OPENSSL_CA_DIR="${WORK_DIR}/ca"
OPENSSL_CA_CONF="${OPENSSL_CA_DIR}/openssl-ca.cnf"

echo "[INFO] generating expired server certificate"
mkdir -p "${OPENSSL_CA_DIR}/newcerts"
: > "${OPENSSL_CA_DIR}/index.txt"
echo "1000" > "${OPENSSL_CA_DIR}/serial"

openssl req -x509 -newkey rsa:2048 -sha256 -nodes -days "${EXPIRED_CA_DAYS}" \
    -subj "${EXPIRED_CA_SUBJECT}" \
    -keyout "${EXPIRED_CA_KEY}" \
    -out "${EXPIRED_CA_CERT}" >/dev/null 2>&1

openssl req -new -newkey rsa:2048 -nodes \
    -subj "${EXPIRED_SERVER_SUBJECT}" \
    -keyout "${EXPIRED_SERVER_KEY}" \
    -out "${EXPIRED_SERVER_CSR}" >/dev/null 2>&1

cat > "${OPENSSL_CA_CONF}" <<'EOF'
[ ca ]
default_ca = CA_default

[ CA_default ]
dir               = __CA_DIR__
new_certs_dir     = $dir/newcerts
database          = $dir/index.txt
serial            = $dir/serial
private_key       = __CA_KEY__
certificate       = __CA_CERT__
default_md        = sha256
policy            = policy_loose
x509_extensions   = server_cert
unique_subject    = no

[ policy_loose ]
commonName = supplied

[ server_cert ]
basicConstraints = CA:FALSE
keyUsage = digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth
subjectAltName = @alt_names

[ alt_names ]
DNS.1 = localhost
IP.1 = 127.0.0.1
EOF

sed -i "s|__CA_DIR__|${OPENSSL_CA_DIR}|g" "${OPENSSL_CA_CONF}"
sed -i "s|__CA_KEY__|${EXPIRED_CA_KEY}|g" "${OPENSSL_CA_CONF}"
sed -i "s|__CA_CERT__|${EXPIRED_CA_CERT}|g" "${OPENSSL_CA_CONF}"

openssl ca -batch -config "${OPENSSL_CA_CONF}" \
    -in "${EXPIRED_SERVER_CSR}" \
    -out "${EXPIRED_SERVER_CERT}" \
    -startdate "${EXPIRED_NOT_BEFORE}" \
    -enddate "${EXPIRED_NOT_AFTER}" >/dev/null 2>&1

ORIG_CERT_BACKUP="${WORK_DIR}/original-server.crt.pem"
ORIG_KEY_BACKUP="${WORK_DIR}/original-server.key.pem"
cp "${TLS_CERT_PATH}" "${ORIG_CERT_BACKUP}"
cp "${TLS_KEY_PATH}" "${ORIG_KEY_BACKUP}"
cp "${EXPIRED_SERVER_CERT}" "${TLS_CERT_PATH}"
cp "${EXPIRED_SERVER_KEY}" "${TLS_KEY_PATH}"

echo "[INFO] starting server with expired certificate: ${SERVER_BIN}"
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

echo "[INFO] running client against expired certificate server"
set +e
timeout "${CLIENT_TIMEOUT_SEC}s" \
    "${CLIENT_BIN}" "${CLIENT_IP}" "${CLIENT_PORT}" "${EXPIRED_CA_CERT}" \
    < /dev/null >"${CLIENT_LOG}" 2>&1
CLIENT_RC=$?
set -e

if [[ "${CLIENT_RC}" -eq 0 ]]; then
    fail "client succeeded unexpectedly with expired certificate"
fi

if [[ "${CLIENT_RC}" -eq 124 ]]; then
    fail "client timed out with expired certificate"
fi

if ! has_any_expired_tls_error "${CLIENT_LOG}"; then
    fail "client log missing TLS expired-classified error (expected tls.verify_cert_expired/tls.verify_failed/tls.handshake_failed)"
fi

sleep "${POST_CHECK_WAIT_SEC}"
if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
    fail "server crashed after expired certificate client attempt"
fi

echo "[PASS] expired certificate test passed"
echo "[INFO] client exit code: ${CLIENT_RC}"
echo "[INFO] server log: ${SERVER_LOG}"
echo "[INFO] client log: ${CLIENT_LOG}"
