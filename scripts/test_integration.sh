#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG_FILE="${TEST_CONFIG:-${ROOT_DIR}/config/test_suite.conf}"
source "${ROOT_DIR}/scripts/test_tls_common.sh"

LOG_DIR="$(resolve_path_from_root "$(cfg_get "suite.log_dir" "test_log")")"
TLS_CONFIG="$(resolve_path_from_root "$(cfg_get "suite.tls_config" "config/test_tls.conf")")"
DB_CONFIG="$(resolve_path_from_root "$(cfg_get "suite.db_config" "config/test_db.conf")")"
FAIL_FAST_RAW="$(cfg_get "suite.fail_fast" "0")"

RUN_NORMAL_RAW="$(cfg_get "suite.run.normal" "1")"
RUN_FORCED_RAW="$(cfg_get "suite.run.forced" "1")"
RUN_MISMATCH_RAW="$(cfg_get "suite.run.mismatch" "1")"
RUN_EXPIRED_RAW="$(cfg_get "suite.run.expired" "1")"
RUN_PLAINTEXT_RAW="$(cfg_get "suite.run.plaintext" "1")"
RUN_STRESS_RAW="$(cfg_get "suite.run.stress" "1")"
RUN_DB_RAW="$(cfg_get "suite.run.db" "1")"

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
    echo "--- ${name} tail ---" >> "${SUITE_LOG}"
    tail -n 40 "${out_log}" >> "${SUITE_LOG}" || true
    echo "--- end ${name} tail ---" >> "${SUITE_LOG}"

    if [[ "${FAIL_FAST}" == "1" ]]; then
        echo "[INFO] fail_fast=1, stopping early" | tee -a "${SUITE_LOG}"
        return 1
    fi
    return 0
}

[[ -x "${ROOT_DIR}/scripts/test_tls_normal_connection.sh" ]] || {
    echo "[FAIL] missing executable: scripts/test_tls_normal_connection.sh"
    exit 1
}
[[ -x "${ROOT_DIR}/scripts/test_tls_forced_termination.sh" ]] || {
    echo "[FAIL] missing executable: scripts/test_tls_forced_termination.sh"
    exit 1
}
[[ -x "${ROOT_DIR}/scripts/test_tls_cert_mismatch.sh" ]] || {
    echo "[FAIL] missing executable: scripts/test_tls_cert_mismatch.sh"
    exit 1
}
[[ -x "${ROOT_DIR}/scripts/test_tls_expired_cert.sh" ]] || {
    echo "[FAIL] missing executable: scripts/test_tls_expired_cert.sh"
    exit 1
}
[[ -x "${ROOT_DIR}/scripts/test_tls_plaintext_reject.sh" ]] || {
    echo "[FAIL] missing executable: scripts/test_tls_plaintext_reject.sh"
    exit 1
}
[[ -x "${ROOT_DIR}/scripts/test_tls_reconnect_stress.sh" ]] || {
    echo "[FAIL] missing executable: scripts/test_tls_reconnect_stress.sh"
    exit 1
}
[[ -x "${ROOT_DIR}/scripts/test_db_connection.sh" ]] || {
    echo "[FAIL] missing executable: scripts/test_db_connection.sh"
    exit 1
}

echo "[INFO] integration suite start ${SUITE_TS}" | tee -a "${SUITE_LOG}"
echo "[INFO] suite config: ${CONFIG_FILE}" | tee -a "${SUITE_LOG}"
echo "[INFO] tls config: ${TLS_CONFIG}" | tee -a "${SUITE_LOG}"
echo "[INFO] db config: ${DB_CONFIG}" | tee -a "${SUITE_LOG}"
echo "[INFO] fail_fast: ${FAIL_FAST}" | tee -a "${SUITE_LOG}"

if is_enabled "${RUN_NORMAL_RAW}"; then
    run_test "tls-normal" "${ROOT_DIR}/scripts/test_tls_normal_connection.sh" "${TLS_CONFIG}" || true
else
    skip_test "tls-normal"
fi
if [[ "${FAIL_FAST}" == "1" && "${FAIL_COUNT}" -gt 0 ]]; then goto_end=1; else goto_end=0; fi

if [[ "${goto_end}" -eq 0 ]]; then
    if is_enabled "${RUN_FORCED_RAW}"; then
        run_test "tls-forced" "${ROOT_DIR}/scripts/test_tls_forced_termination.sh" "${TLS_CONFIG}" || true
    else
        skip_test "tls-forced"
    fi
fi
if [[ "${FAIL_FAST}" == "1" && "${FAIL_COUNT}" -gt 0 ]]; then goto_end=1; fi

if [[ "${goto_end}" -eq 0 ]]; then
    if is_enabled "${RUN_MISMATCH_RAW}"; then
        run_test "tls-mismatch" "${ROOT_DIR}/scripts/test_tls_cert_mismatch.sh" "${TLS_CONFIG}" || true
    else
        skip_test "tls-mismatch"
    fi
fi
if [[ "${FAIL_FAST}" == "1" && "${FAIL_COUNT}" -gt 0 ]]; then goto_end=1; fi

if [[ "${goto_end}" -eq 0 ]]; then
    if is_enabled "${RUN_EXPIRED_RAW}"; then
        run_test "tls-expired" "${ROOT_DIR}/scripts/test_tls_expired_cert.sh" "${TLS_CONFIG}" || true
    else
        skip_test "tls-expired"
    fi
fi
if [[ "${FAIL_FAST}" == "1" && "${FAIL_COUNT}" -gt 0 ]]; then goto_end=1; fi

if [[ "${goto_end}" -eq 0 ]]; then
    if is_enabled "${RUN_PLAINTEXT_RAW}"; then
        run_test "tls-plaintext" "${ROOT_DIR}/scripts/test_tls_plaintext_reject.sh" "${TLS_CONFIG}" || true
    else
        skip_test "tls-plaintext"
    fi
fi
if [[ "${FAIL_FAST}" == "1" && "${FAIL_COUNT}" -gt 0 ]]; then goto_end=1; fi

if [[ "${goto_end}" -eq 0 ]]; then
    if is_enabled "${RUN_STRESS_RAW}"; then
        run_test "tls-stress" "${ROOT_DIR}/scripts/test_tls_reconnect_stress.sh" "${TLS_CONFIG}" || true
    else
        skip_test "tls-stress"
    fi
fi
if [[ "${FAIL_FAST}" == "1" && "${FAIL_COUNT}" -gt 0 ]]; then goto_end=1; fi

if [[ "${goto_end}" -eq 0 ]]; then
    if is_enabled "${RUN_DB_RAW}"; then
        run_test "db-connection" "${ROOT_DIR}/scripts/test_db_connection.sh" "${DB_CONFIG}" || true
    else
        skip_test "db-connection"
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
