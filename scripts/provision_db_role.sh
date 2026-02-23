#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "${ROOT_DIR}/scripts/test/test_tls_common.sh"

SERVER_CONFIG_RAW="${1:-config/server.conf}"
ENV_FILE_RAW="${2:-.env}"

CONNECT_TIMEOUT_SEC="${DB_CONNECT_TIMEOUT_SEC:-5}"

SERVER_CONFIG="$(resolve_path_from_root "${SERVER_CONFIG_RAW}")"
ENV_FILE="$(resolve_path_from_root "${ENV_FILE_RAW}")"

fail() {
    echo "[FAIL] $1"
    exit 1
}

info() {
    echo "[INFO] $1"
}

warn() {
    echo "[WARN] $1"
}

conn_quote() {
    local s="$1"
    s="${s//\\/\\\\}"
    s="${s//\'/\\\'}"
    echo "${s}"
}

build_conninfo() {
    local host="$1"
    local port="$2"
    local user="$3"
    local db="$4"
    local password="$5"
    local sslmode="$6"
    local connect_timeout="$7"
    printf "host=%s port=%s user=%s dbname=%s password=%s sslmode=%s connect_timeout=%s" \
        "${host}" \
        "${port}" \
        "${user}" \
        "${db}" \
        "${password}" \
        "${sslmode}" \
        "${connect_timeout}"
}

trim_wrapping_quotes() {
    local s="$1"
    if [[ ${#s} -ge 2 ]]; then
        local first="${s:0:1}"
        local last="${s: -1}"
        if [[ ( "${first}" == "'" && "${last}" == "'" ) || ( "${first}" == "\"" && "${last}" == "\"" ) ]]; then
            s="${s:1:${#s}-2}"
        fi
    fi
    echo "${s}"
}

sql_literal() {
    local s="$1"
    s="${s//\'/\'\'}"
    printf "'%s'" "${s}"
}

sql_ident() {
    local s="$1"
    s="${s//\"/\"\"}"
    printf "\"%s\"" "${s}"
}

[[ -f "${SERVER_CONFIG}" ]] || fail "server config not found: ${SERVER_CONFIG}"
[[ -f "${ENV_FILE}" ]] || fail "env file not found: ${ENV_FILE}"
command -v psql >/dev/null 2>&1 || fail "psql command not found"

DB_HOST="$(cfg_get_from_file "db.host" "127.0.0.1" "${SERVER_CONFIG}")"
DB_PORT="$(cfg_get_from_file "db.port" "5432" "${SERVER_CONFIG}")"
DB_NAME="$(cfg_get_from_file "db.name" "" "${SERVER_CONFIG}")"
DB_SSLMODE="$(cfg_get_from_file "db.sslmode" "disable" "${SERVER_CONFIG}")"
APP_DB_USER="$(trim_wrapping_quotes "$(cfg_get_from_file "db.user" "" "${ENV_FILE}")")"
APP_DB_PASSWORD="$(trim_wrapping_quotes "$(cfg_get_from_file "db.password" "" "${ENV_FILE}")")"

if [[ -z "${APP_DB_PASSWORD}" ]]; then
    APP_DB_PASSWORD="$(trim_wrapping_quotes "$(cfg_get_from_file "db_password" "" "${ENV_FILE}")")"
fi
if [[ -z "${APP_DB_PASSWORD}" ]]; then
    APP_DB_PASSWORD="$(trim_wrapping_quotes "$(cfg_get_from_file "DB_PASSWORD" "" "${ENV_FILE}")")"
fi

[[ -n "${DB_NAME}" ]] || fail "db.name missing in ${SERVER_CONFIG}"
[[ -n "${DB_SSLMODE}" ]] || DB_SSLMODE="disable"
[[ -n "${APP_DB_USER}" ]] || fail "db.user missing in ${ENV_FILE}"
[[ -n "${APP_DB_PASSWORD}" ]] || fail "db.password missing in ${ENV_FILE}"

ADMIN_USER="$(trim_wrapping_quotes "$(cfg_get_from_file "db.admin_user" "${APP_DB_USER}" "${ENV_FILE}")")"
ADMIN_PASSWORD="$(trim_wrapping_quotes "$(cfg_get_from_file "db.admin_password" "" "${ENV_FILE}")")"
ADMIN_DB="$(trim_wrapping_quotes "$(cfg_get_from_file "db.admin_db" "postgres" "${ENV_FILE}")")"
ADMIN_HOST="${DB_HOST}"
ADMIN_PORT="${DB_PORT}"
ADMIN_USE_PEER_AUTH=0

if [[ -z "${ADMIN_PASSWORD}" && "${ADMIN_USER}" == "${APP_DB_USER}" ]]; then
    ADMIN_PASSWORD="${APP_DB_PASSWORD}"
fi

[[ -n "${ADMIN_USER}" ]] || fail "admin user is empty (db.admin_user in ${ENV_FILE})"
[[ -n "${ADMIN_DB}" ]] || fail "admin db is empty (db.admin_db in ${ENV_FILE})"
[[ -n "${ADMIN_PASSWORD}" ]] || fail "admin password is empty (db.admin_password in ${ENV_FILE})"

is_local_admin_host() {
    [[ "${ADMIN_HOST}" == "127.0.0.1" || "${ADMIN_HOST}" == "localhost" || -z "${ADMIN_HOST}" ]]
}

psql_admin_password() {
    local conninfo
    conninfo="$(build_conninfo "${ADMIN_HOST}" "${ADMIN_PORT}" "${ADMIN_USER}" "${ADMIN_DB}" "${ADMIN_PASSWORD}" "${DB_SSLMODE}" "${CONNECT_TIMEOUT_SEC}")"
    psql "${conninfo}" \
        --no-psqlrc -v ON_ERROR_STOP=1 "$@"
}

psql_as_postgres_peer() {
    if [[ "$(id -u)" -eq 0 ]]; then
        runuser -u postgres -- psql --no-psqlrc -v ON_ERROR_STOP=1 "$@"
        return $?
    fi
    if command -v sudo >/dev/null 2>&1 && sudo -n -u postgres true >/dev/null 2>&1; then
        sudo -n -u postgres psql --no-psqlrc -v ON_ERROR_STOP=1 "$@"
        return $?
    fi
    return 1
}

psql_admin_peer() {
    psql_as_postgres_peer -d "${ADMIN_DB}" "$@"
}

psql_admin() {
    if [[ "${ADMIN_USE_PEER_AUTH}" == "1" ]]; then
        psql_admin_peer "$@"
        return $?
    fi
    psql_admin_password "$@"
}

psql_target() {
    if [[ "${ADMIN_USE_PEER_AUTH}" == "1" ]]; then
        psql_as_postgres_peer -d "${DB_NAME}" "$@"
        return $?
    fi
    local conninfo
    conninfo="$(build_conninfo "${ADMIN_HOST}" "${ADMIN_PORT}" "${ADMIN_USER}" "${DB_NAME}" "${ADMIN_PASSWORD}" "${DB_SSLMODE}" "${CONNECT_TIMEOUT_SEC}")"
    psql "${conninfo}" \
        --no-psqlrc -v ON_ERROR_STOP=1 "$@"
}

APP_DB_USER_LIT="$(sql_literal "${APP_DB_USER}")"
APP_DB_USER_IDENT="$(sql_ident "${APP_DB_USER}")"
APP_DB_PASSWORD_LIT="$(sql_literal "${APP_DB_PASSWORD}")"
DB_NAME_LIT="$(sql_literal "${DB_NAME}")"
DB_NAME_IDENT="$(sql_ident "${DB_NAME}")"

info "provision target db=${DB_NAME} app_user=${APP_DB_USER} admin_user=${ADMIN_USER}"

if ! psql_admin_password -tA -c "SELECT 1;" >/dev/null 2>&1; then
    if [[ "${ADMIN_USER}" == "postgres" ]] && is_local_admin_host; then
        if psql_admin_peer -tA -c "SELECT 1;" >/dev/null 2>&1; then
            ADMIN_USE_PEER_AUTH=1
            warn "admin password auth failed; using local peer auth for postgres"
        else
            fail "admin password auth failed; peer auth fallback also failed (check db.admin_password or local sudo/root access)"
        fi
    else
        fail "admin password auth failed (check db.admin_user/db.admin_password and db.host)"
    fi
fi

if [[ "${APP_DB_USER}" == "postgres" ]]; then
    warn "db.user is postgres; dedicated app role is recommended for production"
fi

role_exists="$(
    psql_admin -tA -c "SELECT 1 FROM pg_roles WHERE rolname = ${APP_DB_USER_LIT} LIMIT 1;"
)"
if [[ "${role_exists}" == "1" ]]; then
    info "role exists, updating password/login: ${APP_DB_USER}"
    psql_admin -c "ALTER ROLE ${APP_DB_USER_IDENT} WITH LOGIN PASSWORD ${APP_DB_PASSWORD_LIT};" >/dev/null
else
    info "creating role: ${APP_DB_USER}"
    psql_admin -c "CREATE ROLE ${APP_DB_USER_IDENT} WITH LOGIN PASSWORD ${APP_DB_PASSWORD_LIT};" >/dev/null
fi

db_exists="$(
    psql_admin -tA -c "SELECT 1 FROM pg_database WHERE datname = ${DB_NAME_LIT} LIMIT 1;"
)"
if [[ "${db_exists}" == "1" ]]; then
    info "database exists, setting owner: ${DB_NAME}"
    psql_admin -c "ALTER DATABASE ${DB_NAME_IDENT} OWNER TO ${APP_DB_USER_IDENT};" >/dev/null
else
    info "creating database: ${DB_NAME}"
    psql_admin -c "CREATE DATABASE ${DB_NAME_IDENT} OWNER ${APP_DB_USER_IDENT};" >/dev/null
fi

info "granting database privileges"
psql_admin -c "GRANT CONNECT, TEMP ON DATABASE ${DB_NAME_IDENT} TO ${APP_DB_USER_IDENT};" >/dev/null

info "ensuring app schemas exist"
psql_target -c "CREATE SCHEMA IF NOT EXISTS auth;" >/dev/null
psql_target -c "CREATE SCHEMA IF NOT EXISTS social;" >/dev/null

info "granting schema/default privileges (public, auth, social)"
psql_target -c "GRANT USAGE, CREATE ON SCHEMA public TO ${APP_DB_USER_IDENT};" >/dev/null
psql_target -c "GRANT USAGE, CREATE ON SCHEMA auth TO ${APP_DB_USER_IDENT};" >/dev/null
psql_target -c "GRANT USAGE, CREATE ON SCHEMA social TO ${APP_DB_USER_IDENT};" >/dev/null
psql_target -c "ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT SELECT, INSERT, UPDATE, DELETE ON TABLES TO ${APP_DB_USER_IDENT};" >/dev/null
psql_target -c "ALTER DEFAULT PRIVILEGES IN SCHEMA auth GRANT SELECT, INSERT, UPDATE, DELETE ON TABLES TO ${APP_DB_USER_IDENT};" >/dev/null
psql_target -c "ALTER DEFAULT PRIVILEGES IN SCHEMA social GRANT SELECT, INSERT, UPDATE, DELETE ON TABLES TO ${APP_DB_USER_IDENT};" >/dev/null
psql_target -c "ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT USAGE, SELECT, UPDATE ON SEQUENCES TO ${APP_DB_USER_IDENT};" >/dev/null
psql_target -c "ALTER DEFAULT PRIVILEGES IN SCHEMA auth GRANT USAGE, SELECT, UPDATE ON SEQUENCES TO ${APP_DB_USER_IDENT};" >/dev/null
psql_target -c "ALTER DEFAULT PRIVILEGES IN SCHEMA social GRANT USAGE, SELECT, UPDATE ON SEQUENCES TO ${APP_DB_USER_IDENT};" >/dev/null

info "granting privileges on existing objects"
psql_target -c "GRANT SELECT, INSERT, UPDATE, DELETE ON ALL TABLES IN SCHEMA public TO ${APP_DB_USER_IDENT};" >/dev/null
psql_target -c "GRANT SELECT, INSERT, UPDATE, DELETE ON ALL TABLES IN SCHEMA auth TO ${APP_DB_USER_IDENT};" >/dev/null
psql_target -c "GRANT SELECT, INSERT, UPDATE, DELETE ON ALL TABLES IN SCHEMA social TO ${APP_DB_USER_IDENT};" >/dev/null
psql_target -c "GRANT USAGE, SELECT, UPDATE ON ALL SEQUENCES IN SCHEMA public TO ${APP_DB_USER_IDENT};" >/dev/null
psql_target -c "GRANT USAGE, SELECT, UPDATE ON ALL SEQUENCES IN SCHEMA auth TO ${APP_DB_USER_IDENT};" >/dev/null
psql_target -c "GRANT USAGE, SELECT, UPDATE ON ALL SEQUENCES IN SCHEMA social TO ${APP_DB_USER_IDENT};" >/dev/null

echo "[PASS] db role/database provisioning complete"
echo "[INFO] server config: ${SERVER_CONFIG}"
echo "[INFO] env file: ${ENV_FILE}"
echo "[INFO] next step: ./scripts/migrate_db_schema.sh ${SERVER_CONFIG_RAW} ${ENV_FILE_RAW}"
