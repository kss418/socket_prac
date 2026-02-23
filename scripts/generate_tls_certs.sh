#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CERT_DIR_RAW="${CERT_DIR:-certs}"
CERT_DIR="${ROOT_DIR}/${CERT_DIR_RAW}"

CA_KEY="${CERT_DIR}/ca.key.pem"
CA_CERT="${CERT_DIR}/ca.crt.pem"
CA_SERIAL="${CERT_DIR}/ca.crt.srl"
SERVER_KEY="${CERT_DIR}/server.key.pem"
SERVER_CSR="${CERT_DIR}/server.csr.pem"
SERVER_CERT="${CERT_DIR}/server.crt.pem"
SERVER_EXT="${CERT_DIR}/server.ext"

CA_DAYS="${CA_DAYS:-3650}"
SERVER_DAYS="${SERVER_DAYS:-825}"
CA_SUBJECT="${CA_SUBJECT:-/CN=socket-prac-local-ca}"
SERVER_SUBJECT="${SERVER_SUBJECT:-/CN=127.0.0.1}"
SERVER_SAN_DNS="${SERVER_SAN_DNS:-localhost}"
SERVER_SAN_IP="${SERVER_SAN_IP:-127.0.0.1}"

fail() {
    echo "[FAIL] $1"
    exit 1
}

info() {
    echo "[INFO] $1"
}

command -v openssl >/dev/null 2>&1 || fail "openssl command not found"

mkdir -p "${CERT_DIR}"

timestamp() {
    date '+%Y%m%d_%H%M%S'
}

backup_existing() {
    local backup_dir=""
    local f=""
    for f in "${CA_KEY}" "${CA_CERT}" "${CA_SERIAL}" "${SERVER_KEY}" "${SERVER_CSR}" "${SERVER_CERT}" "${SERVER_EXT}"; do
        if [[ -f "${f}" ]]; then
            if [[ -z "${backup_dir}" ]]; then
                backup_dir="${CERT_DIR}/backup-$(timestamp)"
                mkdir -p "${backup_dir}"
                info "backing up existing cert files to ${backup_dir}"
            fi
            cp -a "${f}" "${backup_dir}/"
        fi
    done
}

write_server_ext() {
    cat > "${SERVER_EXT}" <<EOF
authorityKeyIdentifier=keyid,issuer
basicConstraints=CA:FALSE
keyUsage = digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth
subjectAltName = DNS:${SERVER_SAN_DNS},IP:${SERVER_SAN_IP}
EOF
}

backup_existing

info "generating CA key/cert"
openssl genrsa -out "${CA_KEY}" 4096 >/dev/null 2>&1
openssl req -x509 -new -sha256 \
    -key "${CA_KEY}" \
    -out "${CA_CERT}" \
    -days "${CA_DAYS}" \
    -subj "${CA_SUBJECT}" >/dev/null 2>&1

info "generating server key/csr"
openssl genrsa -out "${SERVER_KEY}" 2048 >/dev/null 2>&1
openssl req -new \
    -key "${SERVER_KEY}" \
    -out "${SERVER_CSR}" \
    -subj "${SERVER_SUBJECT}" >/dev/null 2>&1

write_server_ext

info "signing server certificate with CA"
openssl x509 -req -sha256 \
    -in "${SERVER_CSR}" \
    -CA "${CA_CERT}" \
    -CAkey "${CA_KEY}" \
    -CAcreateserial \
    -out "${SERVER_CERT}" \
    -days "${SERVER_DAYS}" \
    -extfile "${SERVER_EXT}" >/dev/null 2>&1

chmod 600 "${CA_KEY}" "${SERVER_KEY}"
chmod 644 "${CA_CERT}" "${SERVER_CERT}" "${SERVER_CSR}" "${SERVER_EXT}"
[[ -f "${CA_SERIAL}" ]] && chmod 644 "${CA_SERIAL}" || true

info "verifying certificate chain"
openssl verify -CAfile "${CA_CERT}" "${SERVER_CERT}"

echo "[PASS] tls certificates generated"
echo "[INFO] ca cert: ${CA_CERT}"
echo "[INFO] server cert: ${SERVER_CERT}"
echo "[INFO] server key: ${SERVER_KEY}"
