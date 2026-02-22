#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG_FILE="${TEST_CONFIG:-${ROOT_DIR}/config/test_db.conf}"
source "${ROOT_DIR}/scripts/test_tls_common.sh"

SERVER_CONFIG="$(resolve_path_from_root "$(cfg_get "test.db.server_config" "config/server.conf")")"
LOG_DIR="$(resolve_path_from_root "$(cfg_get "test.db.log_dir" "test_log")")"
DB_QUERY="$(cfg_get "test.db.query" "SELECT 1")"
CONNECT_TIMEOUT_SEC="$(cfg_get "test.db.connect_timeout_sec" "5")"
ENV_FILE="$(resolve_path_from_root "$(cfg_get "test.env_file" ".env")")"
load_env_file "${ENV_FILE}"

mkdir -p "${LOG_DIR}"
DB_LOG="$(make_timestamped_path "${LOG_DIR}" "db-connection-test" "log")"

fail() {
    local msg="$1"
    echo "[FAIL] ${msg}"
    echo "--- db log (${DB_LOG}) ---"
    cat "${DB_LOG}" || true
    exit 1
}

[[ -f "${SERVER_CONFIG}" ]] || fail "server config not found: ${SERVER_CONFIG}"
command -v psql >/dev/null 2>&1 || fail "psql command not found"

DB_HOST="$(cfg_get_from_file "db.host" "127.0.0.1" "${SERVER_CONFIG}")"
DB_PORT="$(cfg_get_from_file "db.port" "5432" "${SERVER_CONFIG}")"
DB_NAME="$(cfg_get_from_file "db.name" "" "${SERVER_CONFIG}")"
DB_USER="$(cfg_get_from_file "db.user" "" "${ENV_FILE}")"
DB_PASSWORD="$(cfg_get_from_file "db.password" "" "${ENV_FILE}")"

if [[ -z "${DB_NAME}" ]]; then
    fail "db.name is missing in ${SERVER_CONFIG}"
fi

if [[ -z "${DB_USER}" ]]; then
    fail "db.user is missing in ${ENV_FILE}"
fi

if [[ -z "${DB_PASSWORD}" ]]; then
    # backward-compat: legacy underscore/uppercase keys
    DB_PASSWORD="$(cfg_get_from_file "db_password" "" "${ENV_FILE}")"
fi

if [[ -z "${DB_PASSWORD}" ]]; then
    DB_PASSWORD="$(cfg_get_from_file "DB_PASSWORD" "" "${ENV_FILE}")"
fi

if [[ -z "${DB_PASSWORD}" ]]; then
    DB_PASSWORD="$(printenv "db.password" 2>/dev/null || true)"
fi

if [[ -z "${DB_PASSWORD}" ]]; then
    DB_PASSWORD="$(printenv "db_password" 2>/dev/null || true)"
fi

if [[ -z "${DB_PASSWORD}" ]]; then
    DB_PASSWORD="$(printenv "DB_PASSWORD" 2>/dev/null || true)"
fi

if [[ ${#DB_PASSWORD} -ge 2 ]]; then
    first="${DB_PASSWORD:0:1}"
    last="${DB_PASSWORD: -1}"
    if [[ ( "${first}" == "'" && "${last}" == "'" ) || ( "${first}" == "\"" && "${last}" == "\"" ) ]]; then
        DB_PASSWORD="${DB_PASSWORD:1:${#DB_PASSWORD}-2}"
    fi
fi

if [[ -z "${DB_PASSWORD}" ]]; then
    fail "db password is empty (set db.password in .env)"
fi

echo "[INFO] testing db connection ${DB_USER}@${DB_HOST}:${DB_PORT}/${DB_NAME}"
set +e
DB_RESULT="$(
    PGCONNECT_TIMEOUT="${CONNECT_TIMEOUT_SEC}" \
    PGPASSWORD="${DB_PASSWORD}" \
    psql -h "${DB_HOST}" -p "${DB_PORT}" -U "${DB_USER}" -d "${DB_NAME}" \
        --no-psqlrc -v ON_ERROR_STOP=1 -tA -c "${DB_QUERY}" 2>>"${DB_LOG}"
)"
PSQL_RC=$?
set -e

if [[ "${PSQL_RC}" -ne 0 ]]; then
    fail "db query failed (rc=${PSQL_RC})"
fi

if [[ -z "${DB_RESULT}" ]]; then
    fail "db query returned empty result"
fi

echo "[INFO] query result: ${DB_RESULT}" >>"${DB_LOG}"
echo "[PASS] db connection test passed"
echo "[INFO] db log: ${DB_LOG}"
