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
LONGRUN_CLIENT_COUNT="$(cfg_get "test.longrun.client_count" "20")"
LONGRUN_DURATION_SEC="$(cfg_get "test.longrun.duration_sec" "120")"
CONNECT_STAGGER_SEC="$(cfg_get "test.longrun.connect_stagger_sec" "0.01")"
CONNECT_WAIT_TRY="$(cfg_get "test.longrun.connect_wait_try" "300")"
CONNECT_WAIT_INTERVAL_SEC="$(cfg_get "test.longrun.connect_wait_interval_sec" "0.1")"
CHECK_INTERVAL_SEC="$(cfg_get "test.longrun.check_interval_sec" "1")"
CLOSE_WAIT_SEC="$(cfg_get "test.longrun.close_wait_sec" "20")"
MIN_UNIQUE_SENDERS_PER_CLIENT="$(cfg_get "test.longrun.min_unique_senders_per_client" "2")"
REGISTER_LOGIN_DELAY_SEC="$(cfg_get "test.longrun.register_login_delay_sec" "0.2")"
LOGIN_SETTLE_SEC="$(cfg_get "test.longrun.login_settle_sec" "1")"
MESSAGE_INTERVAL_SEC="$(cfg_get "test.longrun.message_interval_sec" "5")"
MESSAGE_PREFIX="$(cfg_get "test.longrun.message_prefix" "longrun-msg")"
ENV_FILE="$(resolve_path_from_root "$(cfg_get "test.env_file" ".env")")"
load_env_file "${ENV_FILE}"

mkdir -p "${LOG_DIR}"
RUN_TS="$(timestamp_now)"
RUN_DIR="${LOG_DIR}/tls-concurrent-longrun-${RUN_TS}"
run_seq=1
while [[ -e "${RUN_DIR}" ]]; do
    RUN_DIR="${LOG_DIR}/tls-concurrent-longrun-${RUN_TS}-${run_seq}"
    run_seq=$((run_seq + 1))
done
mkdir -p "${RUN_DIR}"

SERVER_LOG="$(make_timestamped_path "${RUN_DIR}" "tls-server-concurrent-longrun" "log")"
MAIN_CLIENT_LOG="$(make_timestamped_path "${RUN_DIR}" "tls-client-concurrent-longrun" "log")"
: > "${MAIN_CLIENT_LOG}"

SERVER_PID=""
SEARCH_BIN=""
declare -a CLIENT_PIDS=()
declare -a WRITER_PIDS=()
declare -a CLIENT_FIFOS=()
declare -a CLIENT_LOGS=()

if command -v rg >/dev/null 2>&1; then
    SEARCH_BIN="rg"
else
    SEARCH_BIN="grep"
fi

contains() {
    local pattern="$1"
    local file="$2"
    if [[ "${SEARCH_BIN}" == "rg" ]]; then
        rg -q "${pattern}" "${file}"
    else
        grep -q "${pattern}" "${file}"
    fi
}

count_matches() {
    local pattern="$1"
    local file="$2"
    local out=""
    if [[ "${SEARCH_BIN}" == "rg" ]]; then
        out="$(rg -c "${pattern}" "${file}" 2>/dev/null || true)"
    else
        out="$(grep -c "${pattern}" "${file}" 2>/dev/null || true)"
    fi
    if [[ -z "${out}" ]]; then
        echo "0"
    else
        echo "${out}"
    fi
}

count_positive_send_lines() {
    local file="$1"
    local out=""
    if [[ "${SEARCH_BIN}" == "rg" ]]; then
        out="$(rg -c "sends [1-9][0-9]* byte" "${file}" 2>/dev/null || true)"
    else
        out="$(grep -E -c "sends [1-9][0-9]* byte" "${file}" 2>/dev/null || true)"
    fi
    if [[ -z "${out}" ]]; then
        echo "0"
    else
        echo "${out}"
    fi
}

count_unique_senders_in_client_log() {
    local file="$1"
    local out=""
    out="$(
        awk -v prefix="${MESSAGE_PREFIX}" '
        {
            sep = index($0, ": ");
            if(sep == 0) next;

            msg = substr($0, sep + 2);
            needle = prefix "-";
            if(index(msg, needle) != 1) next;

            rest = substr(msg, length(needle) + 1);
            dash = index(rest, "-");
            if(dash == 0) next;

            sender = substr(rest, 1, dash - 1);
            if(sender != "") seen[sender] = 1;
        }
        END {
            c = 0;
            for(k in seen) c++;
            print c + 0;
        }
        ' "${file}" 2>/dev/null
    )"
    if [[ -z "${out}" ]]; then
        echo "0"
    else
        echo "${out}"
    fi
}

cleanup() {
    for pid in "${CLIENT_PIDS[@]:-}"; do
        if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
            kill "${pid}" 2>/dev/null || true
            wait "${pid}" 2>/dev/null || true
        fi
    done

    for pid in "${WRITER_PIDS[@]:-}"; do
        if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
            kill "${pid}" 2>/dev/null || true
            wait "${pid}" 2>/dev/null || true
        fi
    done

    for fifo in "${CLIENT_FIFOS[@]:-}"; do
        rm -f "${fifo}" 2>/dev/null || true
    done

    if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
        kill "${SERVER_PID}" 2>/dev/null || true
        wait "${SERVER_PID}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

fail() {
    local msg="$1"
    echo "[FAIL] ${msg}"
    echo "--- server log (${SERVER_LOG}) ---"
    cat "${SERVER_LOG}" || true
    echo "--- aggregate client log (${MAIN_CLIENT_LOG}) ---"
    cat "${MAIN_CLIENT_LOG}" || true
    echo "--- run dir (${RUN_DIR}) ---"
    exit 1
}

wait_pid_exit_with_timeout() {
    local pid="$1"
    local timeout_sec="$2"
    local end_ts=$((SECONDS + timeout_sec))
    while kill -0 "${pid}" 2>/dev/null; do
        if [[ "${SECONDS}" -ge "${end_ts}" ]]; then
            return 1
        fi
        sleep 0.1
    done
    return 0
}

[[ -x "${SERVER_BIN}" ]] || fail "server binary not found: ${SERVER_BIN}"
[[ -x "${CLIENT_BIN}" ]] || fail "client binary not found: ${CLIENT_BIN}"
[[ "${LONGRUN_CLIENT_COUNT}" =~ ^[0-9]+$ ]] || fail "test.longrun.client_count must be numeric"
[[ "${LONGRUN_CLIENT_COUNT}" -gt 0 ]] || fail "test.longrun.client_count must be > 0"
[[ "${LONGRUN_DURATION_SEC}" =~ ^[0-9]+$ ]] || fail "test.longrun.duration_sec must be numeric"
[[ "${LONGRUN_DURATION_SEC}" -gt 0 ]] || fail "test.longrun.duration_sec must be > 0"
[[ "${CONNECT_WAIT_TRY}" =~ ^[0-9]+$ ]] || fail "test.longrun.connect_wait_try must be numeric"
[[ "${CONNECT_WAIT_TRY}" -gt 0 ]] || fail "test.longrun.connect_wait_try must be > 0"
[[ "${CLOSE_WAIT_SEC}" =~ ^[0-9]+$ ]] || fail "test.longrun.close_wait_sec must be numeric"
[[ "${CLOSE_WAIT_SEC}" -gt 0 ]] || fail "test.longrun.close_wait_sec must be > 0"
[[ "${MIN_UNIQUE_SENDERS_PER_CLIENT}" =~ ^[0-9]+$ ]] || fail "test.longrun.min_unique_senders_per_client must be numeric"
[[ "${MIN_UNIQUE_SENDERS_PER_CLIENT}" -gt 0 ]] || fail "test.longrun.min_unique_senders_per_client must be > 0"
if [[ "${LONGRUN_CLIENT_COUNT}" -lt "${MIN_UNIQUE_SENDERS_PER_CLIENT}" ]]; then
    MIN_UNIQUE_SENDERS_PER_CLIENT="${LONGRUN_CLIENT_COUNT}"
fi

echo "[INFO] starting server: ${SERVER_BIN}"
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

echo "[INFO] launching ${LONGRUN_CLIENT_COUNT} concurrent clients for ${LONGRUN_DURATION_SEC}s"
RUN_ID="$(timestamp_now)"
for ((i=1; i<=LONGRUN_CLIENT_COUNT; ++i)); do
    client_idx="${i}"
    fifo_path="$(mktemp -u "${RUN_DIR}/tls-longrun-client-${client_idx}-stdin-XXXX.fifo")"
    mkfifo "${fifo_path}"
    CLIENT_FIFOS+=("${fifo_path}")

    client_id="lr_${RUN_ID}_${client_idx}"
    client_pw="pw_${RUN_ID}_${client_idx}"
    (
        printf '/register %s %s\n' "${client_id}" "${client_pw}"
        sleep "${REGISTER_LOGIN_DELAY_SEC}"
        printf '/login %s %s\n' "${client_id}" "${client_pw}"
        sleep "${LOGIN_SETTLE_SEC}"

        end_ts=$((SECONDS + LONGRUN_DURATION_SEC))
        msg_seq=1
        while [[ "${SECONDS}" -lt "${end_ts}" ]]; do
            printf '%s-%s-%s\n' "${MESSAGE_PREFIX}" "${client_idx}" "${msg_seq}"
            msg_seq=$((msg_seq + 1))
            sleep "${MESSAGE_INTERVAL_SEC}"
        done
    ) > "${fifo_path}" &
    writer_pid=$!
    WRITER_PIDS+=("${writer_pid}")

    client_log="$(make_timestamped_path "${RUN_DIR}" "tls-client-concurrent-${client_idx}" "log")"
    CLIENT_LOGS+=("${client_log}")

    set +e
    "${CLIENT_BIN}" "${CLIENT_IP}" "${CLIENT_PORT}" < "${fifo_path}" > "${client_log}" 2>&1 &
    client_pid=$!
    set -e
    CLIENT_PIDS+=("${client_pid}")

    {
        echo "[CLIENT ${client_idx}] id=${client_id} pid=${client_pid} log=${client_log}"
    } >> "${MAIN_CLIENT_LOG}"

    if [[ "${CONNECT_STAGGER_SEC}" != "0" ]]; then
        sleep "${CONNECT_STAGGER_SEC}"
    fi
done

connected_ready=0
for ((try=1; try<=CONNECT_WAIT_TRY; ++try)); do
    connected_count="$(count_matches "is connected" "${SERVER_LOG}")"
    if [[ "${connected_count}" -ge "${LONGRUN_CLIENT_COUNT}" ]]; then
        connected_ready=1
        break
    fi

    if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
        fail "server crashed while waiting for initial concurrent connects"
    fi
    sleep "${CONNECT_WAIT_INTERVAL_SEC}"
done

if [[ "${connected_ready}" -ne 1 ]]; then
    fail "did not reach target concurrent connections (want=${LONGRUN_CLIENT_COUNT}, got=${connected_count:-0})"
fi

for ((i=0; i<${#CLIENT_PIDS[@]}; ++i)); do
    pid="${CLIENT_PIDS[$i]}"
    if ! kill -0 "${pid}" 2>/dev/null; then
        cl="${CLIENT_LOGS[$i]}"
        echo "[INFO] client ${i} exited early, log=${cl}" >> "${MAIN_CLIENT_LOG}"
        fail "client process exited early before longrun hold"
    fi
done

echo "[INFO] concurrent clients connected. holding for ${LONGRUN_DURATION_SEC}s"
hold_end=$((SECONDS + LONGRUN_DURATION_SEC))
while [[ "${SECONDS}" -lt "${hold_end}" ]]; do
    if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
        fail "server crashed during longrun hold"
    fi
    sleep "${CHECK_INTERVAL_SEC}"
done

echo "[INFO] hold finished; closing clients"
for pid in "${WRITER_PIDS[@]}"; do
    if kill -0 "${pid}" 2>/dev/null; then
        kill "${pid}" 2>/dev/null || true
    fi
done

for ((i=0; i<${#CLIENT_PIDS[@]}; ++i)); do
    pid="${CLIENT_PIDS[$i]}"
    if ! wait_pid_exit_with_timeout "${pid}" "${CLOSE_WAIT_SEC}"; then
        cl="${CLIENT_LOGS[$i]}"
        echo "[INFO] client timeout pid=${pid} log=${cl}" >> "${MAIN_CLIENT_LOG}"
        fail "client did not exit within close timeout (${CLOSE_WAIT_SEC}s)"
    fi
done

client_fail_count=0
for ((i=0; i<${#CLIENT_PIDS[@]}; ++i)); do
    pid="${CLIENT_PIDS[$i]}"
    set +e
    wait "${pid}"
    rc=$?
    set -e
    if [[ "${rc}" -ne 0 ]]; then
        client_fail_count=$((client_fail_count + 1))
        cl="${CLIENT_LOGS[$i]}"
        echo "[INFO] client nonzero rc=${rc} pid=${pid} log=${cl}" >> "${MAIN_CLIENT_LOG}"
    fi
done

if [[ "${client_fail_count}" -ne 0 ]]; then
    fail "some clients exited with nonzero code (count=${client_fail_count})"
fi

sleep "${POST_CHECK_WAIT_SEC}"
if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
    fail "server crashed after longrun clients closed"
fi

connected_count="$(count_matches "is connected" "${SERVER_LOG}")"
disconnected_count="$(count_matches "is disconnected" "${SERVER_LOG}")"
if [[ "${connected_count}" -lt "${LONGRUN_CLIENT_COUNT}" ]]; then
    fail "connected count is too low (want >= ${LONGRUN_CLIENT_COUNT}, got ${connected_count})"
fi
if [[ "${disconnected_count}" -lt "${LONGRUN_CLIENT_COUNT}" ]]; then
    fail "disconnected count is too low (want >= ${LONGRUN_CLIENT_COUNT}, got ${disconnected_count})"
fi

positive_send_lines="$(count_positive_send_lines "${SERVER_LOG}")"
if [[ "${positive_send_lines}" -lt "${LONGRUN_CLIENT_COUNT}" ]]; then
    fail "server recv payload evidence too low (want >= ${LONGRUN_CLIENT_COUNT}, got ${positive_send_lines})"
fi

login_success_count=0
message_observed_count=0
sender_check_fail_count=0
min_sender_count="${LONGRUN_CLIENT_COUNT}"
for ((i=0; i<${#CLIENT_LOGS[@]}; ++i)); do
    cl="${CLIENT_LOGS[$i]}"
    if contains "login success" "${cl}"; then
        login_success_count=$((login_success_count + 1))
    else
        echo "[INFO] no login success in ${cl}" >> "${MAIN_CLIENT_LOG}"
    fi

    if contains "${MESSAGE_PREFIX}-" "${cl}"; then
        message_observed_count=$((message_observed_count + 1))
    else
        echo "[INFO] no message observed in ${cl}" >> "${MAIN_CLIENT_LOG}"
    fi

    unique_sender_count="$(count_unique_senders_in_client_log "${cl}")"
    if [[ "${unique_sender_count}" -lt "${min_sender_count}" ]]; then
        min_sender_count="${unique_sender_count}"
    fi
    if [[ "${unique_sender_count}" -lt "${MIN_UNIQUE_SENDERS_PER_CLIENT}" ]]; then
        sender_check_fail_count=$((sender_check_fail_count + 1))
        echo "[INFO] sender check failed in ${cl}: unique=${unique_sender_count}, need=${MIN_UNIQUE_SENDERS_PER_CLIENT}" >> "${MAIN_CLIENT_LOG}"
    fi
done

if [[ "${login_success_count}" -lt "${LONGRUN_CLIENT_COUNT}" ]]; then
    fail "login success count is too low (want=${LONGRUN_CLIENT_COUNT}, got=${login_success_count})"
fi

if [[ "${message_observed_count}" -lt "${LONGRUN_CLIENT_COUNT}" ]]; then
    fail "message receive evidence too low (want=${LONGRUN_CLIENT_COUNT}, got=${message_observed_count})"
fi

if [[ "${sender_check_fail_count}" -ne 0 ]]; then
    fail "sender diversity check failed (failed_clients=${sender_check_fail_count}, min_unique_need=${MIN_UNIQUE_SENDERS_PER_CLIENT}, observed_min=${min_sender_count})"
fi

if contains "Protocol error" "${SERVER_LOG}"; then
    fail "server log contains 'Protocol error'"
fi

echo "[PASS] concurrent longrun test passed"
echo "[INFO] clients: ${LONGRUN_CLIENT_COUNT}"
echo "[INFO] hold duration sec: ${LONGRUN_DURATION_SEC}"
echo "[INFO] positive send lines: ${positive_send_lines}"
echo "[INFO] login success count: ${login_success_count}"
echo "[INFO] message observed count: ${message_observed_count}"
echo "[INFO] min unique senders per client: ${min_sender_count}"
echo "[INFO] run dir: ${RUN_DIR}"
echo "[INFO] server log: ${SERVER_LOG}"
echo "[INFO] aggregate client log: ${MAIN_CLIENT_LOG}"
