#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "${ROOT_DIR}/scripts/test_tls_common.sh"

SERVER_CONFIG_RAW="${1:-config/server.conf}"
ENV_FILE_RAW="${2:-.env}"
CREATE_DB_RAW="${CREATE_DB:-1}"
ADMIN_DB_RAW="${DB_ADMIN_DB:-postgres}"
CONNECT_TIMEOUT_SEC="${DB_CONNECT_TIMEOUT_SEC:-5}"

SERVER_CONFIG="$(resolve_path_from_root "${SERVER_CONFIG_RAW}")"
ENV_FILE="$(resolve_path_from_root "${ENV_FILE_RAW}")"

to_bool() {
    local v="$1"
    case "${v,,}" in
        1|true|yes|on) echo "1" ;;
        *) echo "0" ;;
    esac
}

fail() {
    echo "[FAIL] $1"
    exit 1
}

info() {
    echo "[INFO] $1"
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
DB_USER="$(trim_wrapping_quotes "$(cfg_get_from_file "db.user" "" "${ENV_FILE}")")"
DB_PASSWORD="$(trim_wrapping_quotes "$(cfg_get_from_file "db.password" "" "${ENV_FILE}")")"

if [[ -z "${DB_PASSWORD}" ]]; then
    DB_PASSWORD="$(trim_wrapping_quotes "$(cfg_get_from_file "db_password" "" "${ENV_FILE}")")"
fi
if [[ -z "${DB_PASSWORD}" ]]; then
    DB_PASSWORD="$(trim_wrapping_quotes "$(cfg_get_from_file "DB_PASSWORD" "" "${ENV_FILE}")")"
fi

[[ -n "${DB_NAME}" ]] || fail "db.name is missing in ${SERVER_CONFIG}"
[[ -n "${DB_USER}" ]] || fail "db.user is missing in ${ENV_FILE}"
[[ -n "${DB_PASSWORD}" ]] || fail "db.password is missing in ${ENV_FILE}"

CREATE_DB="$(to_bool "${CREATE_DB_RAW}")"
ADMIN_DB="${ADMIN_DB_RAW}"

psql_exec() {
    local db="$1"
    shift
    PGPASSWORD="${DB_PASSWORD}" \
    PGCONNECT_TIMEOUT="${CONNECT_TIMEOUT_SEC}" \
    psql -h "${DB_HOST}" -p "${DB_PORT}" -U "${DB_USER}" -d "${db}" \
        --no-psqlrc -v ON_ERROR_STOP=1 "$@"
}

if [[ "${CREATE_DB}" == "1" ]]; then
    info "checking database exists: ${DB_NAME}"
    DB_NAME_LIT="$(sql_literal "${DB_NAME}")"
    DB_NAME_IDENT="$(sql_ident "${DB_NAME}")"
    exists="$(
        psql_exec "${ADMIN_DB}" -tA -c "SELECT 1 FROM pg_database WHERE datname = ${DB_NAME_LIT} LIMIT 1;"
    )"
    if [[ "${exists}" != "1" ]]; then
        info "creating database: ${DB_NAME}"
        psql_exec "${ADMIN_DB}" -c "CREATE DATABASE ${DB_NAME_IDENT};" >/dev/null
    else
        info "database already exists: ${DB_NAME}"
    fi
fi

info "applying schema migrations to ${DB_USER}@${DB_HOST}:${DB_PORT}/${DB_NAME}"
psql_exec "${DB_NAME}" <<'SQL'
CREATE SCHEMA IF NOT EXISTS auth;
CREATE SCHEMA IF NOT EXISTS social;

CREATE TABLE IF NOT EXISTS auth.users (
    id TEXT PRIMARY KEY,
    pw TEXT NOT NULL,
    nickname TEXT NOT NULL DEFAULT 'guest'
);

ALTER TABLE auth.users ADD COLUMN IF NOT EXISTS nickname TEXT;
UPDATE auth.users SET nickname = 'guest' WHERE nickname IS NULL;
ALTER TABLE auth.users ALTER COLUMN nickname SET DEFAULT 'guest';
ALTER TABLE auth.users ALTER COLUMN nickname SET NOT NULL;

ALTER TABLE auth.users DROP CONSTRAINT IF EXISTS users_nickname_key;

CREATE TABLE IF NOT EXISTS social.friendships (
    user_a_id  TEXT NOT NULL REFERENCES auth.users(id) ON DELETE CASCADE,
    user_b_id  TEXT NOT NULL REFERENCES auth.users(id) ON DELETE CASCADE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    CONSTRAINT friendships_distinct_users CHECK (user_a_id <> user_b_id),
    CONSTRAINT friendships_ordered_pair CHECK (user_a_id < user_b_id),
    CONSTRAINT friendships_pk PRIMARY KEY (user_a_id, user_b_id)
);

ALTER TABLE social.friendships ADD COLUMN IF NOT EXISTS created_at TIMESTAMPTZ NOT NULL DEFAULT now();

DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conname = 'friendships_distinct_users'
          AND conrelid = 'social.friendships'::regclass
    ) THEN
        ALTER TABLE social.friendships
            ADD CONSTRAINT friendships_distinct_users CHECK (user_a_id <> user_b_id);
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conname = 'friendships_ordered_pair'
          AND conrelid = 'social.friendships'::regclass
    ) THEN
        ALTER TABLE social.friendships
            ADD CONSTRAINT friendships_ordered_pair CHECK (user_a_id < user_b_id);
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conname = 'friendships_pk'
          AND conrelid = 'social.friendships'::regclass
    ) THEN
        ALTER TABLE social.friendships
            ADD CONSTRAINT friendships_pk PRIMARY KEY (user_a_id, user_b_id);
    END IF;
END $$;

CREATE TABLE IF NOT EXISTS social.friend_requests (
    from_user_id TEXT NOT NULL REFERENCES auth.users(id) ON DELETE CASCADE,
    to_user_id   TEXT NOT NULL REFERENCES auth.users(id) ON DELETE CASCADE,
    status       TEXT NOT NULL DEFAULT 'pending',
    created_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
    CONSTRAINT friend_requests_distinct_users CHECK (from_user_id <> to_user_id),
    CONSTRAINT friend_requests_status_check CHECK (status IN ('pending', 'accepted', 'rejected', 'canceled')),
    CONSTRAINT friend_requests_pk PRIMARY KEY (from_user_id, to_user_id)
);

ALTER TABLE social.friend_requests ADD COLUMN IF NOT EXISTS status TEXT;
ALTER TABLE social.friend_requests ADD COLUMN IF NOT EXISTS created_at TIMESTAMPTZ NOT NULL DEFAULT now();
ALTER TABLE social.friend_requests ADD COLUMN IF NOT EXISTS updated_at TIMESTAMPTZ NOT NULL DEFAULT now();

UPDATE social.friend_requests SET status = 'pending' WHERE status IS NULL;
ALTER TABLE social.friend_requests ALTER COLUMN status SET DEFAULT 'pending';
ALTER TABLE social.friend_requests ALTER COLUMN status SET NOT NULL;

DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conname = 'friend_requests_distinct_users'
          AND conrelid = 'social.friend_requests'::regclass
    ) THEN
        ALTER TABLE social.friend_requests
            ADD CONSTRAINT friend_requests_distinct_users CHECK (from_user_id <> to_user_id);
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conname = 'friend_requests_status_check'
          AND conrelid = 'social.friend_requests'::regclass
    ) THEN
        ALTER TABLE social.friend_requests
            ADD CONSTRAINT friend_requests_status_check
            CHECK (status IN ('pending', 'accepted', 'rejected', 'canceled'));
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conname = 'friend_requests_pk'
          AND conrelid = 'social.friend_requests'::regclass
    ) THEN
        ALTER TABLE social.friend_requests
            ADD CONSTRAINT friend_requests_pk PRIMARY KEY (from_user_id, to_user_id);
    END IF;
END $$;

CREATE INDEX IF NOT EXISTS idx_friendships_user_a ON social.friendships (user_a_id);
CREATE INDEX IF NOT EXISTS idx_friendships_user_b ON social.friendships (user_b_id);
CREATE INDEX IF NOT EXISTS idx_friend_requests_to_status ON social.friend_requests (to_user_id, status);
SQL

info "schema migration finished"
info "verifying required tables"

users_exists="$(psql_exec "${DB_NAME}" -tA -c "SELECT to_regclass('auth.users') IS NOT NULL;")"
friendships_exists="$(psql_exec "${DB_NAME}" -tA -c "SELECT to_regclass('social.friendships') IS NOT NULL;")"
friend_requests_exists="$(psql_exec "${DB_NAME}" -tA -c "SELECT to_regclass('social.friend_requests') IS NOT NULL;")"

[[ "${users_exists}" == "t" ]] || fail "auth.users missing after migration"
[[ "${friendships_exists}" == "t" ]] || fail "social.friendships missing after migration"
[[ "${friend_requests_exists}" == "t" ]] || fail "social.friend_requests missing after migration"

echo "[PASS] db schema migration applied successfully"
echo "[INFO] config: ${SERVER_CONFIG}"
echo "[INFO] env: ${ENV_FILE}"
