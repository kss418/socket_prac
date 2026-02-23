#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

REMOTE_USER="${REMOTE_USER:-ubuntu}"
REMOTE_HOST="${REMOTE_HOST:-}"
SSH_PORT="${SSH_PORT:-22}"
KEY_PATH="${KEY_PATH:-}"
REMOTE_DIR="${REMOTE_DIR:-/home/ubuntu/socket_prac}"

SERVER_BIN_RAW="${SERVER_BIN:-build/server}"
ENV_FILE_RAW="${ENV_FILE:-.env}"
CONFIG_FILE_RAW="${CONFIG_FILE:-config/server.conf}"
CONFIG_DIR_RAW="${CONFIG_DIR:-config}"
BOOTSTRAP_FILE_RAW="${BOOTSTRAP_FILE:-bootstrap_ubuntu.sh}"
SCRIPTS_DIR_RAW="${SCRIPTS_DIR:-scripts}"
AUTO_TLS_RAW="${AUTO_TLS:-1}"
TLS_SAN_DNS_RAW="${TLS_SAN_DNS:-}"
TLS_SAN_IP_RAW="${TLS_SAN_IP:-}"
PULL_CA_RAW="${PULL_CA:-1}"
LOCAL_CA_OUT_RAW="${LOCAL_CA_OUT:-ca.crt.pem}"

fail() {
    echo "[FAIL] $1"
    exit 1
}

info() {
    echo "[INFO] $1"
}

usage() {
    cat <<'EOF'
Usage:
  ./scripts/deploy_aws_ubuntu.sh --host <public_ip_or_dns> --key <pem_path> [options]

Required:
  --host              EC2 public IP or DNS
  --key               SSH private key path (.pem)

Options:
  --user              SSH user (default: ubuntu)
  --port              SSH port (default: 22)
  --remote-dir        Remote deploy directory (default: /home/ubuntu/socket_prac)
  --server-bin        Local server binary path, relative to project root allowed (default: build/server)
  --env-file          Local env file path, relative to project root allowed (default: .env)
  --config-file       Local server config file path for validation (default: config/server.conf)
  --config-dir        Local config directory to upload (default: config)
  --bootstrap-file    Local bootstrap script path, relative to project root allowed (default: bootstrap_ubuntu.sh)
  --scripts-dir       Local scripts directory path, relative to project root allowed (default: scripts)
  --auto-tls          Regenerate remote TLS cert with host-matching SAN (default: 1)
  --tls-san-dns       Override SAN DNS value for generated cert (default: auto)
  --tls-san-ip        Override SAN IP value for generated cert (default: auto)
  --pull-ca           Download remote certs/ca.crt.pem to local file (default: 1)
  --local-ca-out      Local output path for downloaded CA (default: ca.crt.pem)
  -h, --help          Show this help

Example:
  ./scripts/deploy_aws_ubuntu.sh \
    --host 43.201.47.169 \
    --key ~/instance.pem \
    --remote-dir /home/ubuntu/socket_prac
EOF
}

expand_home_path() {
    local p="$1"
    if [[ "${p}" == ~/* ]]; then
        echo "${HOME}/${p#~/}"
        return 0
    fi
    echo "${p}"
}

resolve_from_root() {
    local p="$1"
    if [[ "${p}" = /* ]]; then
        echo "${p}"
    else
        echo "${ROOT_DIR}/${p}"
    fi
}

to_bool() {
    local v="$1"
    case "${v,,}" in
        1|true|yes|on) echo "1" ;;
        *) echo "0" ;;
    esac
}

is_ipv4() {
    [[ "$1" =~ ^([0-9]{1,3}\.){3}[0-9]{1,3}$ ]]
}

quote_single() {
    local s="$1"
    s="${s//\'/\'\"\'\"\'}"
    printf "'%s'" "${s}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --host)
            [[ $# -ge 2 ]] || fail "--host requires a value"
            REMOTE_HOST="$2"
            shift 2
            ;;
        --key)
            [[ $# -ge 2 ]] || fail "--key requires a value"
            KEY_PATH="$2"
            shift 2
            ;;
        --user)
            [[ $# -ge 2 ]] || fail "--user requires a value"
            REMOTE_USER="$2"
            shift 2
            ;;
        --port)
            [[ $# -ge 2 ]] || fail "--port requires a value"
            SSH_PORT="$2"
            shift 2
            ;;
        --remote-dir)
            [[ $# -ge 2 ]] || fail "--remote-dir requires a value"
            REMOTE_DIR="$2"
            shift 2
            ;;
        --server-bin)
            [[ $# -ge 2 ]] || fail "--server-bin requires a value"
            SERVER_BIN_RAW="$2"
            shift 2
            ;;
        --env-file)
            [[ $# -ge 2 ]] || fail "--env-file requires a value"
            ENV_FILE_RAW="$2"
            shift 2
            ;;
        --config-file)
            [[ $# -ge 2 ]] || fail "--config-file requires a value"
            CONFIG_FILE_RAW="$2"
            shift 2
            ;;
        --config-dir)
            [[ $# -ge 2 ]] || fail "--config-dir requires a value"
            CONFIG_DIR_RAW="$2"
            shift 2
            ;;
        --bootstrap-file)
            [[ $# -ge 2 ]] || fail "--bootstrap-file requires a value"
            BOOTSTRAP_FILE_RAW="$2"
            shift 2
            ;;
        --scripts-dir)
            [[ $# -ge 2 ]] || fail "--scripts-dir requires a value"
            SCRIPTS_DIR_RAW="$2"
            shift 2
            ;;
        --auto-tls)
            [[ $# -ge 2 ]] || fail "--auto-tls requires a value"
            AUTO_TLS_RAW="$2"
            shift 2
            ;;
        --tls-san-dns)
            [[ $# -ge 2 ]] || fail "--tls-san-dns requires a value"
            TLS_SAN_DNS_RAW="$2"
            shift 2
            ;;
        --tls-san-ip)
            [[ $# -ge 2 ]] || fail "--tls-san-ip requires a value"
            TLS_SAN_IP_RAW="$2"
            shift 2
            ;;
        --pull-ca)
            [[ $# -ge 2 ]] || fail "--pull-ca requires a value"
            PULL_CA_RAW="$2"
            shift 2
            ;;
        --local-ca-out)
            [[ $# -ge 2 ]] || fail "--local-ca-out requires a value"
            LOCAL_CA_OUT_RAW="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            fail "unknown argument: $1"
            ;;
    esac
done

command -v ssh >/dev/null 2>&1 || fail "ssh command not found"
command -v scp >/dev/null 2>&1 || fail "scp command not found"

[[ -n "${REMOTE_HOST}" ]] || fail "missing --host"
[[ -n "${KEY_PATH}" ]] || fail "missing --key"
[[ "${SSH_PORT}" =~ ^[0-9]+$ ]] || fail "invalid --port (must be numeric)"
AUTO_TLS="$(to_bool "${AUTO_TLS_RAW}")"
PULL_CA="$(to_bool "${PULL_CA_RAW}")"
TLS_SAN_DNS="${TLS_SAN_DNS_RAW}"
TLS_SAN_IP="${TLS_SAN_IP_RAW}"

KEY_PATH="$(expand_home_path "${KEY_PATH}")"
if [[ "${KEY_PATH}" != /* ]]; then
    KEY_PATH="${PWD}/${KEY_PATH}"
fi

SERVER_BIN="$(resolve_from_root "${SERVER_BIN_RAW}")"
ENV_FILE="$(resolve_from_root "${ENV_FILE_RAW}")"
CONFIG_FILE="$(resolve_from_root "${CONFIG_FILE_RAW}")"
CONFIG_DIR="$(resolve_from_root "${CONFIG_DIR_RAW}")"
BOOTSTRAP_FILE="$(resolve_from_root "${BOOTSTRAP_FILE_RAW}")"
SCRIPTS_DIR="$(resolve_from_root "${SCRIPTS_DIR_RAW}")"

LOCAL_CA_OUT="$(expand_home_path "${LOCAL_CA_OUT_RAW}")"
if [[ "${LOCAL_CA_OUT}" != /* ]]; then
    LOCAL_CA_OUT="${PWD}/${LOCAL_CA_OUT}"
fi

[[ -f "${KEY_PATH}" ]] || fail "key file not found: ${KEY_PATH}"
[[ -f "${SERVER_BIN}" ]] || fail "server binary not found: ${SERVER_BIN}"
[[ -f "${ENV_FILE}" ]] || fail "env file not found: ${ENV_FILE}"
[[ -f "${CONFIG_FILE}" ]] || fail "config file not found: ${CONFIG_FILE}"
[[ -d "${CONFIG_DIR}" ]] || fail "config directory not found: ${CONFIG_DIR}"
[[ -f "${BOOTSTRAP_FILE}" ]] || fail "bootstrap file not found: ${BOOTSTRAP_FILE}"
[[ -d "${SCRIPTS_DIR}" ]] || fail "scripts directory not found: ${SCRIPTS_DIR}"
if [[ "${AUTO_TLS}" == "1" && ! -x "${ROOT_DIR}/scripts/generate_tls_certs.sh" ]]; then
    fail "local scripts/generate_tls_certs.sh is not executable"
fi

if [[ "${REMOTE_DIR}" == *"'"* ]]; then
    fail "remote dir must not contain single quote: ${REMOTE_DIR}"
fi

REMOTE="${REMOTE_USER}@${REMOTE_HOST}"
SSH_OPTS=(
    -i "${KEY_PATH}"
    -p "${SSH_PORT}"
    -o StrictHostKeyChecking=accept-new
)
SCP_OPTS=(
    -i "${KEY_PATH}"
    -P "${SSH_PORT}"
    -o StrictHostKeyChecking=accept-new
)

info "target: ${REMOTE}"
info "remote dir: ${REMOTE_DIR}"
info "creating remote directories"
ssh "${SSH_OPTS[@]}" "${REMOTE}" "mkdir -p '${REMOTE_DIR}' '${REMOTE_DIR}/config'"

info "uploading server binary"
scp "${SCP_OPTS[@]}" "${SERVER_BIN}" "${REMOTE}:${REMOTE_DIR}/server"

info "uploading .env"
scp "${SCP_OPTS[@]}" "${ENV_FILE}" "${REMOTE}:${REMOTE_DIR}/.env"

info "uploading config directory"
scp "${SCP_OPTS[@]}" -r "${CONFIG_DIR}" "${REMOTE}:${REMOTE_DIR}/"

info "uploading bootstrap file"
scp "${SCP_OPTS[@]}" "${BOOTSTRAP_FILE}" "${REMOTE}:${REMOTE_DIR}/bootstrap_ubuntu.sh"

info "uploading scripts directory"
scp "${SCP_OPTS[@]}" -r "${SCRIPTS_DIR}" "${REMOTE}:${REMOTE_DIR}/"

info "setting executable permissions"
ssh "${SSH_OPTS[@]}" "${REMOTE}" "chmod +x '${REMOTE_DIR}/server' '${REMOTE_DIR}/bootstrap_ubuntu.sh'"

if [[ "${AUTO_TLS}" == "1" ]]; then
    if [[ -z "${TLS_SAN_DNS}" || -z "${TLS_SAN_IP}" ]]; then
        if is_ipv4 "${REMOTE_HOST}"; then
            [[ -n "${TLS_SAN_DNS}" ]] || TLS_SAN_DNS="localhost"
            [[ -n "${TLS_SAN_IP}" ]] || TLS_SAN_IP="${REMOTE_HOST}"
        else
            [[ -n "${TLS_SAN_DNS}" ]] || TLS_SAN_DNS="${REMOTE_HOST}"
            [[ -n "${TLS_SAN_IP}" ]] || TLS_SAN_IP="127.0.0.1"
        fi
    fi

    info "regenerating remote TLS cert (SAN DNS=${TLS_SAN_DNS}, SAN IP=${TLS_SAN_IP})"
    REMOTE_SUBJECT="/CN=${REMOTE_HOST}"
    REMOTE_TLS_CMD="cd $(quote_single "${REMOTE_DIR}") && \
SERVER_SUBJECT=$(quote_single "${REMOTE_SUBJECT}") \
SERVER_SAN_DNS=$(quote_single "${TLS_SAN_DNS}") \
SERVER_SAN_IP=$(quote_single "${TLS_SAN_IP}") \
./scripts/generate_tls_certs.sh"
    ssh "${SSH_OPTS[@]}" "${REMOTE}" "${REMOTE_TLS_CMD}"
fi

if [[ "${PULL_CA}" == "1" ]]; then
    info "downloading remote CA cert -> ${LOCAL_CA_OUT}"
    mkdir -p "$(dirname "${LOCAL_CA_OUT}")"
    scp "${SCP_OPTS[@]}" "${REMOTE}:${REMOTE_DIR}/certs/ca.crt.pem" "${LOCAL_CA_OUT}"
fi

echo "[PASS] deploy files uploaded"
echo "[INFO] uploaded files:"
echo "  - ${REMOTE_DIR}/server"
echo "  - ${REMOTE_DIR}/.env"
echo "  - ${REMOTE_DIR}/config/"
echo "  - ${REMOTE_DIR}/bootstrap_ubuntu.sh"
echo "  - ${REMOTE_DIR}/scripts/"
if [[ "${AUTO_TLS}" == "1" ]]; then
    echo "  - ${REMOTE_DIR}/certs/{ca.crt.pem,server.crt.pem,server.key.pem}"
fi
if [[ "${PULL_CA}" == "1" ]]; then
    echo "  - local CA: ${LOCAL_CA_OUT}"
    echo "[INFO] client example: ./build/client ${REMOTE_HOST} 8080 ${LOCAL_CA_OUT}"
fi
echo "[INFO] next: ssh -i ${KEY_PATH} ${REMOTE} 'cd ${REMOTE_DIR} && ./server'"
