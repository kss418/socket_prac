#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CONFIG_FILE="${TEST_CONFIG:-${ROOT_DIR}/config/test_db.conf}"
source "${ROOT_DIR}/scripts/test/test_tls_common.sh"

SERVER_CONFIG="$(resolve_path_from_root "$(cfg_get "test.db.server_config" "config/server.conf")")"
LOG_DIR="$(resolve_path_from_root "$(cfg_get "test.db.log_dir" "test_log")")"
CONNECT_TIMEOUT_SEC="$(cfg_get "test.db.connect_timeout_sec" "5")"
ENV_FILE="$(resolve_path_from_root "$(cfg_get "test.env_file" ".env")")"
load_env_file "${ENV_FILE}"

mkdir -p "${LOG_DIR}"
DB_LOG="$(make_timestamped_path "${LOG_DIR}" "db-room-feature-test" "log")"
: > "${DB_LOG}"

fail() {
    local msg="$1"
    echo "[FAIL] ${msg}"
    echo "--- db log (${DB_LOG}) ---"
    cat "${DB_LOG}" || true
    exit 1
}

info() {
    echo "[INFO] $1" | tee -a "${DB_LOG}"
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

expect_eq() {
    local got="$1"
    local want="$2"
    local msg="$3"
    if [[ "${got}" != "${want}" ]]; then
        fail "${msg} (want=${want}, got=${got:-<empty>})"
    fi
}

expect_non_empty() {
    local got="$1"
    local msg="$2"
    [[ -n "${got}" ]] || fail "${msg}"
}

psql_exec() {
    local sql="$1"
    PGPASSWORD="${DB_PASSWORD}" \
    PGSSLMODE="${DB_SSLMODE}" \
    PGCONNECT_TIMEOUT="${CONNECT_TIMEOUT_SEC}" \
    psql \
        --host="${DB_HOST}" \
        --port="${DB_PORT}" \
        --username="${DB_USER}" \
        --dbname="${DB_NAME}" \
        --no-psqlrc -v ON_ERROR_STOP=1 -q -tA -c "${sql}"
}

invite_attempt_count() {
    local inviter="$1"
    local invitee="$2"
    psql_exec "WITH ins AS (
        INSERT INTO chat.room_members (room_id, user_id, role)
        SELECT ${ROOM_ID}, '${invitee}', 'member'
        WHERE EXISTS (
            SELECT 1
            FROM chat.room_members
            WHERE room_id = ${ROOM_ID} AND user_id = '${inviter}'
            LIMIT 1
        )
        AND EXISTS (
            SELECT 1
            FROM social.friendships
            WHERE user_a_id = LEAST('${inviter}', '${invitee}')
              AND user_b_id = GREATEST('${inviter}', '${invitee}')
            LIMIT 1
        )
        ON CONFLICT (room_id, user_id) DO NOTHING
        RETURNING user_id
    )
    SELECT count(*) FROM ins;"
}

message_insert_count() {
    local sender="$1"
    local body="$2"
    psql_exec "WITH ins AS (
        INSERT INTO chat.messages (room_id, sender_user_id, body)
        SELECT ${ROOM_ID}, '${sender}', '${body}'
        WHERE EXISTS (
            SELECT 1
            FROM chat.room_members
            WHERE room_id = ${ROOM_ID} AND user_id = '${sender}'
        )
        RETURNING id
    )
    SELECT count(*) FROM ins;"
}

leave_result() {
    local user_id="$1"
    psql_exec "WITH owner_rows AS (
        SELECT owner_user_id
        FROM chat.rooms
        WHERE id = ${ROOM_ID}
        LIMIT 1
    ),
    leave_rows AS (
        DELETE FROM chat.room_members
        WHERE room_id = ${ROOM_ID}
          AND user_id = '${user_id}'
          AND EXISTS (
            SELECT 1
            FROM owner_rows
            WHERE owner_user_id <> '${user_id}'
          )
        RETURNING room_id
    )
    SELECT CASE
        WHEN NOT EXISTS (SELECT 1 FROM owner_rows) THEN 'not_member_or_room_not_found'
        WHEN EXISTS (SELECT 1 FROM owner_rows WHERE owner_user_id = '${user_id}') THEN 'owner_cannot_leave'
        WHEN EXISTS (SELECT 1 FROM leave_rows) THEN 'left'
        ELSE 'not_member_or_room_not_found'
    END;"
}

[[ -f "${SERVER_CONFIG}" ]] || fail "server config not found: ${SERVER_CONFIG}"
command -v psql >/dev/null 2>&1 || fail "psql command not found"

DB_HOST="$(cfg_get_from_file "db.host" "127.0.0.1" "${SERVER_CONFIG}")"
DB_PORT="$(cfg_get_from_file "db.port" "5432" "${SERVER_CONFIG}")"
DB_NAME="$(cfg_get_from_file "db.name" "" "${SERVER_CONFIG}")"
DB_SSLMODE="$(cfg_get_from_file "db.sslmode" "disable" "${SERVER_CONFIG}")"
DB_USER="$(cfg_get_from_file "db.user" "" "${ENV_FILE}")"
DB_PASSWORD="$(cfg_get_from_file "db.password" "" "${ENV_FILE}")"

DB_USER="$(trim_wrapping_quotes "${DB_USER}")"
DB_PASSWORD="$(trim_wrapping_quotes "${DB_PASSWORD}")"

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
OWNER_USER="room_owner_${RUN_ID}"
FRIEND_USER="room_friend_${RUN_ID}"
OUTSIDER_USER="room_out_${RUN_ID}"
ROOM_NAME="room_${RUN_ID}"
ROOM_ID=""

cleanup() {
    set +e
    psql_exec "DELETE FROM auth.users WHERE id IN ('${OWNER_USER}', '${FRIEND_USER}', '${OUTSIDER_USER}');" >/dev/null
}
trap cleanup EXIT

info "testing room feature queries on ${DB_USER}@${DB_HOST}:${DB_PORT}/${DB_NAME}"

expect_eq "$(psql_exec "SELECT to_regclass('chat.rooms') IS NOT NULL;")" "t" "chat.rooms table not found"
expect_eq "$(psql_exec "SELECT to_regclass('chat.room_members') IS NOT NULL;")" "t" "chat.room_members table not found"
expect_eq "$(psql_exec "SELECT to_regclass('chat.messages') IS NOT NULL;")" "t" "chat.messages table not found"
expect_eq "$(psql_exec "SELECT to_regclass('social.friendships') IS NOT NULL;")" "t" "social.friendships table not found"

psql_exec "INSERT INTO auth.users (id, pw, nickname) VALUES ('${OWNER_USER}', 'pw', 'guest') ON CONFLICT (id) DO NOTHING;" >/dev/null
psql_exec "INSERT INTO auth.users (id, pw, nickname) VALUES ('${FRIEND_USER}', 'pw', 'guest') ON CONFLICT (id) DO NOTHING;" >/dev/null
psql_exec "INSERT INTO auth.users (id, pw, nickname) VALUES ('${OUTSIDER_USER}', 'pw', 'guest') ON CONFLICT (id) DO NOTHING;" >/dev/null

# owner-friend relation for invite_room success path
psql_exec "INSERT INTO social.friendships (user_a_id, user_b_id)
           VALUES (LEAST('${OWNER_USER}', '${FRIEND_USER}'), GREATEST('${OWNER_USER}', '${FRIEND_USER}'))
           ON CONFLICT (user_a_id, user_b_id) DO NOTHING;" >/dev/null

# create_room + owner membership insert
ROOM_ID="$(psql_exec "INSERT INTO chat.rooms (name, owner_user_id)
                      VALUES ('${ROOM_NAME}', '${OWNER_USER}')
                      RETURNING id;")"
expect_non_empty "${ROOM_ID}" "create room failed (empty room id)"

owner_member_insert_count="$(psql_exec "WITH ins AS (
    INSERT INTO chat.room_members (room_id, user_id, role)
    VALUES (${ROOM_ID}, '${OWNER_USER}', 'owner')
    ON CONFLICT (room_id, user_id) DO NOTHING
    RETURNING user_id
)
SELECT count(*) FROM ins;")"
expect_eq "${owner_member_insert_count}" "1" "owner member insert expected 1"

owner_role="$(psql_exec "SELECT role FROM chat.room_members WHERE room_id=${ROOM_ID} AND user_id='${OWNER_USER}';")"
expect_eq "${owner_role}" "owner" "owner role mismatch"

owner_list_count="$(psql_exec "SELECT count(*) FROM (
    SELECT r.id
    FROM chat.rooms r
    JOIN chat.room_members scope_m ON scope_m.room_id = r.id AND scope_m.user_id = '${OWNER_USER}'
) t;")"
expect_eq "${owner_list_count}" "1" "owner list_room scope mismatch"

friend_list_before_invite="$(psql_exec "SELECT count(*) FROM (
    SELECT r.id
    FROM chat.rooms r
    JOIN chat.room_members scope_m ON scope_m.room_id = r.id AND scope_m.user_id = '${FRIEND_USER}'
) t;")"
expect_eq "${friend_list_before_invite}" "0" "friend should not see room before invite"

invite_outsider_no_permission_count="$(invite_attempt_count "${OUTSIDER_USER}" "${FRIEND_USER}")"
expect_eq "${invite_outsider_no_permission_count}" "0" "non-member inviter should not invite"

invite_not_friend_count="$(invite_attempt_count "${OWNER_USER}" "${OUTSIDER_USER}")"
expect_eq "${invite_not_friend_count}" "0" "invite non-friend should not be allowed"

invite_friend_count="$(invite_attempt_count "${OWNER_USER}" "${FRIEND_USER}")"
expect_eq "${invite_friend_count}" "1" "invite friend expected 1"

invite_friend_dup_count="$(invite_attempt_count "${OWNER_USER}" "${FRIEND_USER}")"
expect_eq "${invite_friend_dup_count}" "0" "duplicate invite should be blocked"

friend_role="$(psql_exec "SELECT role FROM chat.room_members WHERE room_id=${ROOM_ID} AND user_id='${FRIEND_USER}';")"
expect_eq "${friend_role}" "member" "friend role mismatch after invite"

friend_scope_row="$(psql_exec "SELECT r.id || '|' || r.name || '|' || r.owner_user_id || '|' || COUNT(all_m.user_id)::BIGINT
    FROM chat.rooms r
    JOIN chat.room_members scope_m ON scope_m.room_id = r.id AND scope_m.user_id = '${FRIEND_USER}'
    LEFT JOIN chat.room_members all_m ON all_m.room_id = r.id
    GROUP BY r.id, r.name, r.owner_user_id
    ORDER BY r.id ASC
    LIMIT 1;")"
expect_eq "${friend_scope_row}" "${ROOM_ID}|${ROOM_NAME}|${OWNER_USER}|2" "friend list_room row mismatch"

outsider_msg_count="$(message_insert_count "${OUTSIDER_USER}" "msg-from-outsider")"
expect_eq "${outsider_msg_count}" "0" "outsider message should be blocked"

owner_msg_count="$(message_insert_count "${OWNER_USER}" "msg-from-owner")"
expect_eq "${owner_msg_count}" "1" "owner message insert expected 1"

friend_msg_count="$(message_insert_count "${FRIEND_USER}" "msg-from-friend")"
expect_eq "${friend_msg_count}" "1" "friend message insert expected 1"

room_message_total="$(psql_exec "SELECT count(*) FROM chat.messages WHERE room_id=${ROOM_ID};")"
expect_eq "${room_message_total}" "2" "room message total mismatch"

owner_leave="$(leave_result "${OWNER_USER}")"
expect_eq "${owner_leave}" "owner_cannot_leave" "owner leave policy mismatch"

friend_leave_first="$(leave_result "${FRIEND_USER}")"
expect_eq "${friend_leave_first}" "left" "friend first leave should be left"

friend_leave_second="$(leave_result "${FRIEND_USER}")"
expect_eq "${friend_leave_second}" "not_member_or_room_not_found" "friend second leave should be not_member_or_room_not_found"

friend_list_after_leave="$(psql_exec "SELECT count(*) FROM (
    SELECT r.id
    FROM chat.rooms r
    JOIN chat.room_members scope_m ON scope_m.room_id = r.id AND scope_m.user_id = '${FRIEND_USER}'
) t;")"
expect_eq "${friend_list_after_leave}" "0" "friend should not see room after leave"

friend_msg_after_leave="$(message_insert_count "${FRIEND_USER}" "msg-after-leave")"
expect_eq "${friend_msg_after_leave}" "0" "friend message after leave should be blocked"

delete_non_owner_count="$(psql_exec "WITH del AS (
    DELETE FROM chat.rooms
    WHERE id = ${ROOM_ID} AND owner_user_id = '${FRIEND_USER}'
    RETURNING id
)
SELECT count(*) FROM del;")"
expect_eq "${delete_non_owner_count}" "0" "non-owner delete should be blocked"

delete_owner_count="$(psql_exec "WITH del AS (
    DELETE FROM chat.rooms
    WHERE id = ${ROOM_ID} AND owner_user_id = '${OWNER_USER}'
    RETURNING id
)
SELECT count(*) FROM del;")"
expect_eq "${delete_owner_count}" "1" "owner delete expected 1"

room_exists_after_delete="$(psql_exec "SELECT count(*) FROM chat.rooms WHERE id=${ROOM_ID};")"
expect_eq "${room_exists_after_delete}" "0" "room should be deleted"

member_exists_after_delete="$(psql_exec "SELECT count(*) FROM chat.room_members WHERE room_id=${ROOM_ID};")"
expect_eq "${member_exists_after_delete}" "0" "room_members should be deleted by cascade"

message_exists_after_delete="$(psql_exec "SELECT count(*) FROM chat.messages WHERE room_id=${ROOM_ID};")"
expect_eq "${message_exists_after_delete}" "0" "messages should be deleted by cascade"

echo "[PASS] db room feature test passed"
echo "[INFO] db log: ${DB_LOG}"
