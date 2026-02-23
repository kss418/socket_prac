#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CONFIG_FILE="${TEST_CONFIG:-${ROOT_DIR}/config/test_suite.conf}"
source "${ROOT_DIR}/scripts/test/test_tls_common.sh"

LOG_DIR="$(resolve_path_from_root "$(cfg_get "suite.log_dir" "test_log")")"
TLS_CONFIG="$(resolve_path_from_root "$(cfg_get "suite.tls_config" "config/test_tls.conf")")"
DB_CONFIG="$(resolve_path_from_root "$(cfg_get "suite.db_config" "config/test_db.conf")")"
FAIL_FAST_RAW="$(cfg_get "suite.fail_fast" "0")"
TLS_PORT="$(cfg_get_from_file "test.client_port" "8080" "${TLS_CONFIG}")"
ENV_FILE="$(resolve_path_from_root "$(cfg_get_from_file "test.env_file" ".env" "${TLS_CONFIG}")")"
SERVER_CONFIG="$(resolve_path_from_root "$(cfg_get_from_file "test.db.server_config" "config/server.conf" "${DB_CONFIG}")")"

RUN_NORMAL_RAW="$(cfg_get "suite.run.normal" "1")"
RUN_FORCED_RAW="$(cfg_get "suite.run.forced" "1")"
RUN_MISMATCH_RAW="$(cfg_get "suite.run.mismatch" "1")"
RUN_EXPIRED_RAW="$(cfg_get "suite.run.expired" "1")"
RUN_PLAINTEXT_RAW="$(cfg_get "suite.run.plaintext" "1")"
RUN_STRESS_RAW="$(cfg_get "suite.run.stress" "1")"
RUN_GRACEFUL_RAW="$(cfg_get "suite.run.graceful" "1")"
RUN_LONGRUN_RAW="$(cfg_get "suite.run.longrun" "0")"
RUN_DB_RAW="$(cfg_get "suite.run.db" "1")"
RUN_DB_FRIEND_RAW="$(cfg_get "suite.run.db_friend" "1")"

mkdir -p "${LOG_DIR}"
SUITE_TS="$(timestamp_now)"
SUITE_LOG="${LOG_DIR}/integration-suite-${SUITE_TS}.log"

TOTAL_COUNT=0
PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0

declare -a FAILED_TESTS=()
declare -a SKIPPED_TESTS=()

to_bool() {
    local v="$1"
    case "${v,,}" in
        1|true|yes|on) echo "1" ;;
        *) echo "0" ;;
    esac
}

is_enabled() {
    [[ "$(to_bool "$1")" == "1" ]]
}

trim_wrapping_quotes() {
    local v="$1"
    if [[ ${#v} -ge 2 ]]; then
        local first="${v:0:1}"
        local last="${v: -1}"
        if [[ ( "${first}" == "'" && "${last}" == "'" ) || ( "${first}" == "\"" && "${last}" == "\"" ) ]]; then
            v="${v:1:${#v}-2}"
        fi
    fi
    echo "${v}"
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

single_line() {
    echo "$1" | tr '\n' ' ' | sed -E 's/[[:space:]]+/ /g; s/^[[:space:]]+//; s/[[:space:]]+$//'
}

suite_fail_fast() {
    local code="$1"
    local cause="$2"
    local log_path="${3:-}"
    if [[ -z "${log_path}" ]]; then
        log_path="${SUITE_LOG}"
    fi
    local msg="[FAIL-FAST] code=${code} cause=\"$(single_line "${cause}")\""
    if [[ -n "${log_path}" ]]; then
        msg="${msg} log=${log_path}"
    fi
    echo "${msg}" | tee -a "${SUITE_LOG}"
    exit 1
}

need_tls_tests() {
    if is_enabled "${RUN_NORMAL_RAW}"; then return 0; fi
    if is_enabled "${RUN_FORCED_RAW}"; then return 0; fi
    if is_enabled "${RUN_MISMATCH_RAW}"; then return 0; fi
    if is_enabled "${RUN_EXPIRED_RAW}"; then return 0; fi
    if is_enabled "${RUN_PLAINTEXT_RAW}"; then return 0; fi
    if is_enabled "${RUN_STRESS_RAW}"; then return 0; fi
    if is_enabled "${RUN_GRACEFUL_RAW}"; then return 0; fi
    if is_enabled "${RUN_LONGRUN_RAW}"; then return 0; fi
    return 1
}

need_default_ca_tests() {
    if is_enabled "${RUN_NORMAL_RAW}"; then return 0; fi
    if is_enabled "${RUN_FORCED_RAW}"; then return 0; fi
    if is_enabled "${RUN_PLAINTEXT_RAW}"; then return 0; fi
    if is_enabled "${RUN_STRESS_RAW}"; then return 0; fi
    if is_enabled "${RUN_GRACEFUL_RAW}"; then return 0; fi
    if is_enabled "${RUN_LONGRUN_RAW}"; then return 0; fi
    return 1
}

port_owner_lines() {
    local port="$1"
    if command -v ss >/dev/null 2>&1; then
        ss -ltnp 2>/dev/null | grep -E "[[:space:]]:${port}[[:space:]]" || true
        return 0
    fi
    return 0
}

check_tls_port_available() {
    if ! need_tls_tests; then
        return 0
    fi

    local owners
    owners="$(port_owner_lines "${TLS_PORT}")"
    if [[ -n "${owners}" ]]; then
        suite_fail_fast "E_PORT_IN_USE" "port ${TLS_PORT} already in use (${owners})"
    fi
}

check_required_file() {
    local path="$1"
    local code="$2"
    local desc="$3"
    if [[ ! -f "${path}" ]]; then
        suite_fail_fast "${code}" "${desc}: ${path}"
    fi
}

check_db_ready() {
    local db_host db_port db_name db_sslmode db_user db_password db_conninfo db_result db_rc
    db_host="$(cfg_get_from_file "db.host" "127.0.0.1" "${SERVER_CONFIG}")"
    db_port="$(cfg_get_from_file "db.port" "5432" "${SERVER_CONFIG}")"
    db_name="$(cfg_get_from_file "db.name" "" "${SERVER_CONFIG}")"
    db_sslmode="$(cfg_get_from_file "db.sslmode" "disable" "${SERVER_CONFIG}")"
    db_user="$(trim_wrapping_quotes "$(cfg_get_from_file "db.user" "" "${ENV_FILE}")")"
    db_password="$(trim_wrapping_quotes "$(cfg_get_from_file "db.password" "" "${ENV_FILE}")")"

    if [[ -z "${db_name}" ]]; then
        suite_fail_fast "E_DB_CONFIG_INVALID" "db.name missing in ${SERVER_CONFIG}"
    fi
    if [[ -z "${db_user}" ]]; then
        suite_fail_fast "E_DB_CONFIG_INVALID" "db.user missing in ${ENV_FILE}"
    fi
    if [[ -z "${db_sslmode}" ]]; then
        db_sslmode="disable"
    fi
    if [[ -z "${db_password}" ]]; then
        suite_fail_fast "E_DB_CONFIG_INVALID" "db.password missing in ${ENV_FILE}"
    fi

    db_conninfo="$(build_conninfo "${db_host}" "${db_port}" "${db_user}" "${db_name}" "${db_password}" "${db_sslmode}" "3")"

    set +e
    db_result="$(
        psql "${db_conninfo}" \
            --no-psqlrc -v ON_ERROR_STOP=1 -tA -c "SELECT 1"
    )"
    db_rc=$?
    set -e

    if [[ "${db_rc}" -ne 0 ]]; then
        suite_fail_fast "E_DB_UNAVAILABLE" "${db_result}"
    fi
}

check_suite_prerequisites() {
    if ! need_tls_tests && ! is_enabled "${RUN_DB_RAW}" && ! is_enabled "${RUN_DB_FRIEND_RAW}"; then
        return 0
    fi

    if [[ ! -f "${TLS_CONFIG}" ]]; then
        suite_fail_fast "E_CONFIG_MISSING" "tls config not found: ${TLS_CONFIG}"
    fi
    if [[ ! -f "${DB_CONFIG}" ]]; then
        suite_fail_fast "E_CONFIG_MISSING" "db config not found: ${DB_CONFIG}"
    fi
    if [[ ! -f "${SERVER_CONFIG}" ]]; then
        suite_fail_fast "E_CONFIG_MISSING" "server config not found: ${SERVER_CONFIG}"
    fi
    if [[ ! -f "${ENV_FILE}" ]]; then
        suite_fail_fast "E_CONFIG_MISSING" "env file not found: ${ENV_FILE}"
    fi

    if ! command -v psql >/dev/null 2>&1; then
        suite_fail_fast "E_TOOL_MISSING" "psql command not found"
    fi
    if need_tls_tests && ! command -v ss >/dev/null 2>&1; then
        suite_fail_fast "E_TOOL_MISSING" "ss command not found"
    fi
    if (is_enabled "${RUN_MISMATCH_RAW}" || is_enabled "${RUN_EXPIRED_RAW}") && ! command -v openssl >/dev/null 2>&1; then
        suite_fail_fast "E_TOOL_MISSING" "openssl command not found"
    fi

    if need_tls_tests; then
        local tls_cert_rel tls_key_rel tls_cert_path tls_key_path default_ca_path
        tls_cert_rel="$(cfg_get_from_file "tls.cert" "" "${SERVER_CONFIG}")"
        tls_key_rel="$(cfg_get_from_file "tls.key" "" "${SERVER_CONFIG}")"
        if [[ -z "${tls_cert_rel}" ]]; then
            suite_fail_fast "E_TLS_CONFIG_INVALID" "tls.cert missing in ${SERVER_CONFIG}"
        fi
        if [[ -z "${tls_key_rel}" ]]; then
            suite_fail_fast "E_TLS_CONFIG_INVALID" "tls.key missing in ${SERVER_CONFIG}"
        fi
        tls_cert_path="$(resolve_path_from_root "${tls_cert_rel}")"
        tls_key_path="$(resolve_path_from_root "${tls_key_rel}")"
        check_required_file "${tls_cert_path}" "E_TLS_CERT_MISSING" "tls cert not found"
        check_required_file "${tls_key_path}" "E_TLS_KEY_MISSING" "tls key not found"
        if need_default_ca_tests; then
            default_ca_path="${ROOT_DIR}/certs/ca.crt.pem"
            check_required_file "${default_ca_path}" "E_CA_MISSING" "ca cert not found"
        fi
    fi

    check_db_ready
    check_tls_port_available
}

FAIL_FAST="$(to_bool "${FAIL_FAST_RAW}")"

skip_test() {
    local name="$1"
    SKIP_COUNT=$((SKIP_COUNT + 1))
    SKIPPED_TESTS+=("${name}")
    echo "[SKIP] ${name}" | tee -a "${SUITE_LOG}"
}

run_test() {
    local name="$1"
    local script_path="$2"
    local test_config="$3"

    TOTAL_COUNT=$((TOTAL_COUNT + 1))
    local out_log
    out_log="${LOG_DIR}/integration-${name}-${SUITE_TS}.log"

    echo "[INFO] [${name}] start (${script_path})" | tee -a "${SUITE_LOG}"

    set +e
    TEST_CONFIG="${test_config}" "${script_path}" >"${out_log}" 2>&1
    local rc=$?
    set -e

    if [[ "${rc}" -eq 0 ]]; then
        PASS_COUNT=$((PASS_COUNT + 1))
        echo "[PASS] [${name}] rc=${rc} log=${out_log}" | tee -a "${SUITE_LOG}"
        return 0
    fi

    FAIL_COUNT=$((FAIL_COUNT + 1))
    FAILED_TESTS+=("${name}")
    echo "[FAIL] [${name}] rc=${rc} log=${out_log}" | tee -a "${SUITE_LOG}"
    local tail_line
    tail_line="$(tail -n 40 "${out_log}" 2>/dev/null | awk 'NF{last=$0} END{print last}')"
    if [[ -z "${tail_line}" ]]; then
        tail_line="no output"
    fi
    echo "[FAIL-DETAIL] [${name}] cause=\"$(single_line "${tail_line}")\" log=${out_log}" | tee -a "${SUITE_LOG}"

    if [[ "${FAIL_FAST}" == "1" ]]; then
        echo "[INFO] fail_fast=1, stopping early" | tee -a "${SUITE_LOG}"
        return 1
    fi
    return 0
}

[[ -x "${ROOT_DIR}/scripts/test/test_tls_normal_connection.sh" ]] || {
    echo "[FAIL] missing executable: scripts/test/test_tls_normal_connection.sh"
    exit 1
}
[[ -x "${ROOT_DIR}/scripts/test/test_tls_forced_termination.sh" ]] || {
    echo "[FAIL] missing executable: scripts/test/test_tls_forced_termination.sh"
    exit 1
}
[[ -x "${ROOT_DIR}/scripts/test/test_tls_cert_mismatch.sh" ]] || {
    echo "[FAIL] missing executable: scripts/test/test_tls_cert_mismatch.sh"
    exit 1
}
[[ -x "${ROOT_DIR}/scripts/test/test_tls_expired_cert.sh" ]] || {
    echo "[FAIL] missing executable: scripts/test/test_tls_expired_cert.sh"
    exit 1
}
[[ -x "${ROOT_DIR}/scripts/test/test_tls_plaintext_reject.sh" ]] || {
    echo "[FAIL] missing executable: scripts/test/test_tls_plaintext_reject.sh"
    exit 1
}
[[ -x "${ROOT_DIR}/scripts/test/test_tls_reconnect_stress.sh" ]] || {
    echo "[FAIL] missing executable: scripts/test/test_tls_reconnect_stress.sh"
    exit 1
}
[[ -x "${ROOT_DIR}/scripts/test/test_tls_concurrent_longrun.sh" ]] || {
    echo "[FAIL] missing executable: scripts/test/test_tls_concurrent_longrun.sh"
    exit 1
}
[[ -x "${ROOT_DIR}/scripts/test/test_server_graceful_shutdown.sh" ]] || {
    echo "[FAIL] missing executable: scripts/test/test_server_graceful_shutdown.sh"
    exit 1
}
[[ -x "${ROOT_DIR}/scripts/test/test_db_connection.sh" ]] || {
    echo "[FAIL] missing executable: scripts/test/test_db_connection.sh"
    exit 1
}
[[ -x "${ROOT_DIR}/scripts/test/test_db_friend_features.sh" ]] || {
    echo "[FAIL] missing executable: scripts/test/test_db_friend_features.sh"
    exit 1
}

echo "[INFO] integration suite start ${SUITE_TS}" | tee -a "${SUITE_LOG}"
echo "[INFO] suite config: ${CONFIG_FILE}" | tee -a "${SUITE_LOG}"
echo "[INFO] tls config: ${TLS_CONFIG}" | tee -a "${SUITE_LOG}"
echo "[INFO] db config: ${DB_CONFIG}" | tee -a "${SUITE_LOG}"
echo "[INFO] fail_fast: ${FAIL_FAST}" | tee -a "${SUITE_LOG}"
echo "[INFO] tls port: ${TLS_PORT}" | tee -a "${SUITE_LOG}"
echo "[INFO] env file: ${ENV_FILE}" | tee -a "${SUITE_LOG}"
echo "[INFO] server config: ${SERVER_CONFIG}" | tee -a "${SUITE_LOG}"

load_env_file "${ENV_FILE}"
check_suite_prerequisites

if is_enabled "${RUN_NORMAL_RAW}"; then
    run_test "tls-normal" "${ROOT_DIR}/scripts/test/test_tls_normal_connection.sh" "${TLS_CONFIG}" || true
else
    skip_test "tls-normal"
fi
if [[ "${FAIL_FAST}" == "1" && "${FAIL_COUNT}" -gt 0 ]]; then goto_end=1; else goto_end=0; fi

if [[ "${goto_end}" -eq 0 ]]; then
    if is_enabled "${RUN_FORCED_RAW}"; then
        run_test "tls-forced" "${ROOT_DIR}/scripts/test/test_tls_forced_termination.sh" "${TLS_CONFIG}" || true
    else
        skip_test "tls-forced"
    fi
fi
if [[ "${FAIL_FAST}" == "1" && "${FAIL_COUNT}" -gt 0 ]]; then goto_end=1; fi

if [[ "${goto_end}" -eq 0 ]]; then
    if is_enabled "${RUN_MISMATCH_RAW}"; then
        run_test "tls-mismatch" "${ROOT_DIR}/scripts/test/test_tls_cert_mismatch.sh" "${TLS_CONFIG}" || true
    else
        skip_test "tls-mismatch"
    fi
fi
if [[ "${FAIL_FAST}" == "1" && "${FAIL_COUNT}" -gt 0 ]]; then goto_end=1; fi

if [[ "${goto_end}" -eq 0 ]]; then
    if is_enabled "${RUN_EXPIRED_RAW}"; then
        run_test "tls-expired" "${ROOT_DIR}/scripts/test/test_tls_expired_cert.sh" "${TLS_CONFIG}" || true
    else
        skip_test "tls-expired"
    fi
fi
if [[ "${FAIL_FAST}" == "1" && "${FAIL_COUNT}" -gt 0 ]]; then goto_end=1; fi

if [[ "${goto_end}" -eq 0 ]]; then
    if is_enabled "${RUN_PLAINTEXT_RAW}"; then
        run_test "tls-plaintext" "${ROOT_DIR}/scripts/test/test_tls_plaintext_reject.sh" "${TLS_CONFIG}" || true
    else
        skip_test "tls-plaintext"
    fi
fi
if [[ "${FAIL_FAST}" == "1" && "${FAIL_COUNT}" -gt 0 ]]; then goto_end=1; fi

if [[ "${goto_end}" -eq 0 ]]; then
    if is_enabled "${RUN_STRESS_RAW}"; then
        run_test "tls-stress" "${ROOT_DIR}/scripts/test/test_tls_reconnect_stress.sh" "${TLS_CONFIG}" || true
    else
        skip_test "tls-stress"
    fi
fi
if [[ "${FAIL_FAST}" == "1" && "${FAIL_COUNT}" -gt 0 ]]; then goto_end=1; fi

if [[ "${goto_end}" -eq 0 ]]; then
    if is_enabled "${RUN_GRACEFUL_RAW}"; then
        run_test "server-graceful" "${ROOT_DIR}/scripts/test/test_server_graceful_shutdown.sh" "${TLS_CONFIG}" || true
    else
        skip_test "server-graceful"
    fi
fi
if [[ "${FAIL_FAST}" == "1" && "${FAIL_COUNT}" -gt 0 ]]; then goto_end=1; fi

if [[ "${goto_end}" -eq 0 ]]; then
    if is_enabled "${RUN_LONGRUN_RAW}"; then
        run_test "tls-longrun" "${ROOT_DIR}/scripts/test/test_tls_concurrent_longrun.sh" "${TLS_CONFIG}" || true
    else
        skip_test "tls-longrun"
    fi
fi
if [[ "${FAIL_FAST}" == "1" && "${FAIL_COUNT}" -gt 0 ]]; then goto_end=1; fi

if [[ "${goto_end}" -eq 0 ]]; then
    if is_enabled "${RUN_DB_RAW}"; then
        run_test "db-connection" "${ROOT_DIR}/scripts/test/test_db_connection.sh" "${DB_CONFIG}" || true
    else
        skip_test "db-connection"
    fi
fi
if [[ "${FAIL_FAST}" == "1" && "${FAIL_COUNT}" -gt 0 ]]; then goto_end=1; fi

if [[ "${goto_end}" -eq 0 ]]; then
    if is_enabled "${RUN_DB_FRIEND_RAW}"; then
        run_test "db-friend" "${ROOT_DIR}/scripts/test/test_db_friend_features.sh" "${DB_CONFIG}" || true
    else
        skip_test "db-friend"
    fi
fi

echo "[INFO] summary total=${TOTAL_COUNT} pass=${PASS_COUNT} fail=${FAIL_COUNT} skip=${SKIP_COUNT}" | tee -a "${SUITE_LOG}"
if [[ "${#FAILED_TESTS[@]}" -gt 0 ]]; then
    echo "[INFO] failed tests: ${FAILED_TESTS[*]}" | tee -a "${SUITE_LOG}"
fi
if [[ "${#SKIPPED_TESTS[@]}" -gt 0 ]]; then
    echo "[INFO] skipped tests: ${SKIPPED_TESTS[*]}" | tee -a "${SUITE_LOG}"
fi
echo "[INFO] suite log: ${SUITE_LOG}" | tee -a "${SUITE_LOG}"

if [[ "${FAIL_COUNT}" -gt 0 ]]; then
    exit 1
fi
exit 0
