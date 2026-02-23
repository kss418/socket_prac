#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CONFIG_FILE="${TEST_CONFIG:-${ROOT_DIR}/config/test_db.conf}"
source "${ROOT_DIR}/scripts/lib/common.sh"
source "${ROOT_DIR}/scripts/lib/pg.sh"

SERVER_CONFIG="$(resolve_path_from_root "$(cfg_get "test.db.server_config" "config/server.conf")")"
LOG_DIR="$(resolve_path_from_root "$(cfg_get "test.db.log_dir" "test_log")")"
CONNECT_TIMEOUT_SEC="$(cfg_get "test.db.connect_timeout_sec" "5")"
ENV_FILE="$(resolve_path_from_root "$(cfg_get "test.env_file" ".env")")"
load_env_file "${ENV_FILE}"

mkdir -p "${LOG_DIR}"
DB_LOG="$(make_timestamped_path "${LOG_DIR}" "db-friend-feature-test" "log")"
: > "${DB_LOG}"

fail() {
    local msg="$1"
    echo "[FAIL] ${msg}"
    echo "--- db log (${DB_LOG}) ---"
    cat "${DB_LOG}" || true
    exit 1
}

info() {
    echo "[INFO] $1"
    echo "[INFO] $1" >>"${DB_LOG}"
}

psql_exec() {
    local sql="$1"
    pg_psql "${DB_HOST}" "${DB_PORT}" "${DB_USER}" "${DB_NAME}" "${DB_PASSWORD}" "${DB_SSLMODE}" "${CONNECT_TIMEOUT_SEC}" \
        -tA -c "${sql}"
}

[[ -f "${SERVER_CONFIG}" ]] || fail "server config not found: ${SERVER_CONFIG}"
command -v psql >/dev/null 2>&1 || fail "psql command not found"

DB_HOST="$(cfg_get_from_file "db.host" "127.0.0.1" "${SERVER_CONFIG}")"
DB_PORT="$(cfg_get_from_file "db.port" "5432" "${SERVER_CONFIG}")"
DB_NAME="$(cfg_get_from_file "db.name" "" "${SERVER_CONFIG}")"
DB_SSLMODE="$(cfg_get_from_file "db.sslmode" "disable" "${SERVER_CONFIG}")"
DB_USER="$(trim_wrapping_quotes "$(cfg_get_from_file "db.user" "" "${ENV_FILE}")")"
DB_PASSWORD="$(trim_wrapping_quotes "$(cfg_get_from_file "db.password" "" "${ENV_FILE}")")"

if [[ -z "${DB_PASSWORD}" ]]; then
    DB_PASSWORD="$(trim_wrapping_quotes "$(cfg_get_from_file "db_password" "" "${ENV_FILE}")")"
fi
if [[ -z "${DB_PASSWORD}" ]]; then
    DB_PASSWORD="$(trim_wrapping_quotes "$(cfg_get_from_file "DB_PASSWORD" "" "${ENV_FILE}")")"
fi

[[ -n "${DB_NAME}" ]] || fail "db.name is missing in ${SERVER_CONFIG}"
[[ -n "${DB_SSLMODE}" ]] || DB_SSLMODE="disable"
[[ -n "${DB_USER}" ]] || fail "db.user is missing in ${ENV_FILE}"
[[ -n "${DB_PASSWORD}" ]] || fail "db.password is missing in ${ENV_FILE}"

RUN_ID="$(date '+%Y%m%d_%H%M%S')_${RANDOM}"
USER_A="fr_a_${RUN_ID}"
USER_B="fr_b_${RUN_ID}"

cleanup() {
    set +e
    psql_exec "DELETE FROM social.friendships WHERE user_a_id IN ('${USER_A}', '${USER_B}') OR user_b_id IN ('${USER_A}', '${USER_B}');" >/dev/null
    psql_exec "DELETE FROM social.friend_requests WHERE from_user_id IN ('${USER_A}', '${USER_B}') OR to_user_id IN ('${USER_A}', '${USER_B}');" >/dev/null
    psql_exec "DELETE FROM auth.users WHERE id IN ('${USER_A}', '${USER_B}');" >/dev/null
}
trap cleanup EXIT

info "testing friend feature queries on ${DB_USER}@${DB_HOST}:${DB_PORT}/${DB_NAME}"

# Sanity check: required tables exist
[[ "$(psql_exec "SELECT to_regclass('social.friend_requests') IS NOT NULL;")" == "t" ]] || fail "social.friend_requests table not found"
[[ "$(psql_exec "SELECT to_regclass('social.friendships') IS NOT NULL;")" == "t" ]] || fail "social.friendships table not found"

# Seed users
psql_exec "INSERT INTO auth.users (id, pw, nickname) VALUES ('${USER_A}', 'pw', 'guest') ON CONFLICT (id) DO NOTHING;" >/dev/null
psql_exec "INSERT INTO auth.users (id, pw, nickname) VALUES ('${USER_B}', 'pw', 'guest') ON CONFLICT (id) DO NOTHING;" >/dev/null

# 1) friend request insert (pending)
request_insert_count="$(
    psql_exec "WITH ins AS (
        INSERT INTO social.friend_requests (from_user_id, to_user_id, status)
        SELECT '${USER_A}', '${USER_B}', 'pending'
        WHERE NOT EXISTS (
            SELECT 1 FROM social.friendships
            WHERE user_a_id = LEAST('${USER_A}', '${USER_B}')
              AND user_b_id = GREATEST('${USER_A}', '${USER_B}')
        )
        ON CONFLICT (from_user_id, to_user_id) DO UPDATE
        SET status = 'pending', updated_at = now()
        WHERE social.friend_requests.status IN ('rejected', 'canceled')
        RETURNING from_user_id
    )
    SELECT count(*) FROM ins;"
)"
[[ "${request_insert_count}" == "1" ]] || fail "first friend request insert expected 1, got ${request_insert_count}"

pending_sender="$(
    psql_exec "SELECT string_agg(from_user_id, ',') FROM (
        SELECT from_user_id
        FROM social.friend_requests
        WHERE to_user_id = '${USER_B}' AND status = 'pending'
        ORDER BY from_user_id
    ) t;"
)"
[[ "${pending_sender}" == "${USER_A}" ]] || fail "pending request list mismatch: expected ${USER_A}, got ${pending_sender:-<empty>}"

# 2) duplicate pending request should be blocked
duplicate_pending_count="$(
    psql_exec "WITH ins AS (
        INSERT INTO social.friend_requests (from_user_id, to_user_id, status)
        SELECT '${USER_A}', '${USER_B}', 'pending'
        WHERE NOT EXISTS (
            SELECT 1 FROM social.friendships
            WHERE user_a_id = LEAST('${USER_A}', '${USER_B}')
              AND user_b_id = GREATEST('${USER_A}', '${USER_B}')
        )
        ON CONFLICT (from_user_id, to_user_id) DO UPDATE
        SET status = 'pending', updated_at = now()
        WHERE social.friend_requests.status IN ('rejected', 'canceled')
        RETURNING from_user_id
    )
    SELECT count(*) FROM ins;"
)"
[[ "${duplicate_pending_count}" == "0" ]] || fail "duplicate pending request expected 0, got ${duplicate_pending_count}"

# 3) reject request
reject_count="$(
    psql_exec "WITH upd AS (
        UPDATE social.friend_requests
        SET status = 'rejected', updated_at = now()
        WHERE from_user_id = '${USER_A}'
          AND to_user_id = '${USER_B}'
          AND status = 'pending'
        RETURNING from_user_id
    )
    SELECT count(*) FROM upd;"
)"
[[ "${reject_count}" == "1" ]] || fail "reject expected 1, got ${reject_count}"

# 4) re-request after reject should be allowed (upsert to pending)
rerequest_count="$(
    psql_exec "WITH ins AS (
        INSERT INTO social.friend_requests (from_user_id, to_user_id, status)
        SELECT '${USER_A}', '${USER_B}', 'pending'
        WHERE NOT EXISTS (
            SELECT 1 FROM social.friendships
            WHERE user_a_id = LEAST('${USER_A}', '${USER_B}')
              AND user_b_id = GREATEST('${USER_A}', '${USER_B}')
        )
        ON CONFLICT (from_user_id, to_user_id) DO UPDATE
        SET status = 'pending', updated_at = now()
        WHERE social.friend_requests.status IN ('rejected', 'canceled')
        RETURNING from_user_id
    )
    SELECT count(*) FROM ins;"
)"
[[ "${rerequest_count}" == "1" ]] || fail "re-request after reject expected 1, got ${rerequest_count}"

# 5) accept request -> friendship created
accept_count="$(
    psql_exec "WITH upd AS (
        UPDATE social.friend_requests
        SET status = 'accepted', updated_at = now()
        WHERE from_user_id = '${USER_A}'
          AND to_user_id = '${USER_B}'
          AND status = 'pending'
        RETURNING from_user_id
    ),
    ins AS (
        INSERT INTO social.friendships (user_a_id, user_b_id)
        SELECT LEAST('${USER_A}', '${USER_B}'), GREATEST('${USER_A}', '${USER_B}')
        FROM upd
        ON CONFLICT (user_a_id, user_b_id) DO NOTHING
        RETURNING user_a_id
    )
    SELECT count(*) FROM upd;"
)"
[[ "${accept_count}" == "1" ]] || fail "accept expected 1, got ${accept_count}"

friend_list_a="$(
    psql_exec "SELECT string_agg(friend_id, ',') FROM (
        SELECT CASE
            WHEN user_a_id = '${USER_A}' THEN user_b_id
            ELSE user_a_id
        END AS friend_id
        FROM social.friendships
        WHERE user_a_id = '${USER_A}' OR user_b_id = '${USER_A}'
        ORDER BY friend_id
    ) t;"
)"
[[ "${friend_list_a}" == "${USER_B}" ]] || fail "friend list expected ${USER_B}, got ${friend_list_a:-<empty>}"

# 6) request when already friends should be blocked
request_when_friend_count="$(
    psql_exec "WITH ins AS (
        INSERT INTO social.friend_requests (from_user_id, to_user_id, status)
        SELECT '${USER_A}', '${USER_B}', 'pending'
        WHERE NOT EXISTS (
            SELECT 1 FROM social.friendships
            WHERE user_a_id = LEAST('${USER_A}', '${USER_B}')
              AND user_b_id = GREATEST('${USER_A}', '${USER_B}')
        )
        ON CONFLICT (from_user_id, to_user_id) DO UPDATE
        SET status = 'pending', updated_at = now()
        WHERE social.friend_requests.status IN ('rejected', 'canceled')
        RETURNING from_user_id
    )
    SELECT count(*) FROM ins;"
)"
[[ "${request_when_friend_count}" == "0" ]] || fail "request when already friend expected 0, got ${request_when_friend_count}"

# 7) remove friend
remove_count="$(
    psql_exec "WITH del AS (
        DELETE FROM social.friendships
        WHERE user_a_id = LEAST('${USER_A}', '${USER_B}')
          AND user_b_id = GREATEST('${USER_A}', '${USER_B}')
        RETURNING user_a_id
    )
    SELECT count(*) FROM del;"
)"
[[ "${remove_count}" == "1" ]] || fail "friend remove expected 1, got ${remove_count}"

friend_list_after_remove="$(
    psql_exec "SELECT count(*) FROM social.friendships
        WHERE user_a_id = LEAST('${USER_A}', '${USER_B}')
          AND user_b_id = GREATEST('${USER_A}', '${USER_B}');"
)"
[[ "${friend_list_after_remove}" == "0" ]] || fail "friend relation should be removed, still exists"

echo "[PASS] db friend feature test passed"
echo "[INFO] db log: ${DB_LOG}"
