#!/usr/bin/env bash
set -eEuo pipefail

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
PROGRESS_INTERVAL_SEC="$(cfg_get "test.longrun.progress_interval_sec" "30")"
MESSAGE_PREFIX="$(cfg_get "test.longrun.message_prefix" "longrun-msg")"
ENV_FILE="$(resolve_path_from_root "$(cfg_get "test.env_file" ".env")")"
load_env_file "${ENV_FILE}"

mkdir -p "${LOG_DIR}"
LOCK_FILE="${LOG_DIR}/.tls-concurrent-longrun.lock"
if command -v flock >/dev/null 2>&1; then
    exec 9>"${LOCK_FILE}"
    if ! flock -n 9; then
        echo "[FAIL] another test_tls_concurrent_longrun.sh is already running"
        exit 1
    fi
fi

RUN_TS="$(timestamp_now)"
RUN_DIR="${LOG_DIR}/tls-concurrent-longrun-${RUN_TS}"
CLIENT_ARGV0_PREFIX="tls-longrun-client-${RUN_TS}"
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
CLEANUP_DONE=0
FAILING=0
SELF_PID="$$"
declare -a CLIENT_PIDS=()
declare -a CLIENT_FIFOS=()
declare -a CLIENT_FIFO_FDS=()
declare -a CLIENT_LOGS=()
declare -a CLIENT_MSG_SEQ=()
declare -a CLIENT_IDS=()
declare -a CLIENT_PWS=()

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

kill_pid_list() {
    local pid_list="$1"
    local force="${2:-0}"
    local pid=""
    while read -r pid; do
        [[ -z "${pid}" ]] && continue
        [[ "${pid}" =~ ^[0-9]+$ ]] || continue
        [[ "${pid}" -eq "${SELF_PID}" ]] && continue
        if [[ "${force}" -eq 1 ]]; then
            kill -9 "${pid}" 2>/dev/null || true
        else
            kill "${pid}" 2>/dev/null || true
        fi
    done <<< "${pid_list}"
}

precleanup_stale_longrun_processes() {
    [[ -x "${CLIENT_BIN}" ]] || return 0

    if command -v pgrep >/dev/null 2>&1; then
        # Stale tagged clients from previous runs.
        stale_tagged_clients="$(pgrep -f '^tls-longrun-client-.*' || true)"
        if [[ -n "${stale_tagged_clients}" ]]; then
            kill_pid_list "${stale_tagged_clients}" 0
            sleep 0.2
            stale_tagged_clients="$(pgrep -f '^tls-longrun-client-.*' || true)"
            kill_pid_list "${stale_tagged_clients}" 1
        fi

        # Stale longrun script shells left by abnormal termination.
        stale_script_shells="$(pgrep -f 'bash ./test_tls_concurrent_longrun.sh|bash .*/test_tls_concurrent_longrun.sh' || true)"
        if [[ -n "${stale_script_shells}" ]]; then
            kill_pid_list "${stale_script_shells}" 0
            sleep 0.2
            stale_script_shells="$(pgrep -f 'bash ./test_tls_concurrent_longrun.sh|bash .*/test_tls_concurrent_longrun.sh' || true)"
            kill_pid_list "${stale_script_shells}" 1
        fi

        # Stale clients launched by older versions (untagged argv0).
        stale_old_clients="$(pgrep -f "^${CLIENT_BIN} ${CLIENT_IP} ${CLIENT_PORT}( |$)" || true)"
        if [[ -n "${stale_old_clients}" ]]; then
            kill_pid_list "${stale_old_clients}" 0
            sleep 0.2
            stale_old_clients="$(pgrep -f "^${CLIENT_BIN} ${CLIENT_IP} ${CLIENT_PORT}( |$)" || true)"
            kill_pid_list "${stale_old_clients}" 1
        fi
    fi
}

cleanup() {
    if [[ "${CLEANUP_DONE}" -eq 1 ]]; then
        return
    fi
    CLEANUP_DONE=1

    # Terminate all direct background jobs of this shell first.
    job_pids="$(jobs -pr 2>/dev/null || true)"
    if [[ -n "${job_pids}" ]]; then
        while read -r pid; do
            [[ -z "${pid}" ]] && continue
            kill "${pid}" 2>/dev/null || true
        done <<< "${job_pids}"
        while read -r pid; do
            [[ -z "${pid}" ]] && continue
            wait "${pid}" 2>/dev/null || true
        done <<< "${job_pids}"
    fi

    for fd in "${CLIENT_FIFO_FDS[@]:-}"; do
        [[ -z "${fd}" ]] && continue
        eval "exec ${fd}>&-" 2>/dev/null || true
    done

    for pid in "${CLIENT_PIDS[@]:-}"; do
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

    # Last resort for remaining shell jobs.
    job_pids="$(jobs -pr 2>/dev/null || true)"
    if [[ -n "${job_pids}" ]]; then
        while read -r pid; do
            [[ -z "${pid}" ]] && continue
            kill -9 "${pid}" 2>/dev/null || true
            wait "${pid}" 2>/dev/null || true
        done <<< "${job_pids}"
    fi

    # Fallback: reap any tagged client processes that may have escaped job tracking.
    if command -v pgrep >/dev/null 2>&1; then
        tagged_client_pids="$(pgrep -f "^${CLIENT_ARGV0_PREFIX}-[0-9]+( |$)" || true)"
        if [[ -n "${tagged_client_pids}" ]]; then
            while read -r pid; do
                [[ -z "${pid}" ]] && continue
                kill "${pid}" 2>/dev/null || true
            done <<< "${tagged_client_pids}"
            sleep 0.2
            tagged_client_pids="$(pgrep -f "^${CLIENT_ARGV0_PREFIX}-[0-9]+( |$)" || true)"
            if [[ -n "${tagged_client_pids}" ]]; then
                while read -r pid; do
                    [[ -z "${pid}" ]] && continue
                    kill -9 "${pid}" 2>/dev/null || true
                done <<< "${tagged_client_pids}"
            fi
        fi
    fi
}
trap cleanup EXIT INT TERM HUP

print_file_head() {
    local file="$1"
    local max_lines="${2:-120}"
    local count=0
    local line=""

    [[ -f "${file}" ]] || return 0
    while IFS= read -r line; do
        printf '%s\n' "${line}"
        count=$((count + 1))
        if [[ "${count}" -ge "${max_lines}" ]]; then
            echo "[INFO] log truncated at ${max_lines} lines"
            break
        fi
    done < "${file}"
}

print_fork_limit_diag() {
    local nproc_limit=""
    local nofile_limit=""
    local cgpath=""
    local cgroup_base=""
    local pids_max=""
    local pids_cur=""

    nproc_limit="$(ulimit -u 2>/dev/null || true)"
    nofile_limit="$(ulimit -n 2>/dev/null || true)"
    [[ -n "${nproc_limit}" ]] && echo "[INFO] nproc limit (ulimit -u): ${nproc_limit}"
    [[ -n "${nofile_limit}" ]] && echo "[INFO] nofile limit (ulimit -n): ${nofile_limit}"

    cgpath="$(awk -F: '$2=="pids"{print $3; exit}' /proc/self/cgroup 2>/dev/null || true)"
    if [[ -z "${cgpath}" ]]; then
        cgpath="$(awk -F: '$1=="0"{print $3; exit}' /proc/self/cgroup 2>/dev/null || true)"
    fi

    if [[ -n "${cgpath}" ]]; then
        for cgroup_base in "/sys/fs/cgroup${cgpath}" "/sys/fs/cgroup/pids${cgpath}"; do
            if [[ -f "${cgroup_base}/pids.max" && -f "${cgroup_base}/pids.current" ]]; then
                pids_max="$(cat "${cgroup_base}/pids.max" 2>/dev/null || true)"
                pids_cur="$(cat "${cgroup_base}/pids.current" 2>/dev/null || true)"
                if [[ -n "${pids_max}" && -n "${pids_cur}" ]]; then
                    echo "[INFO] cgroup pids: ${pids_cur}/${pids_max}"
                fi
                break
            fi
        done
    fi
}

fail() {
    local msg="$1"
    set +e
    if [[ "${FAILING}" -eq 1 ]]; then
        exit 1
    fi
    FAILING=1

    echo "[FAIL] ${msg}"
    echo "--- server log (${SERVER_LOG}) ---"
    print_file_head "${SERVER_LOG}" || true
    echo "--- aggregate client log (${MAIN_CLIENT_LOG}) ---"
    print_file_head "${MAIN_CLIENT_LOG}" || true
    if [[ "${msg}" == *"fork/resource limit"* || "${msg}" == *"fork"* ]]; then
        print_fork_limit_diag
    fi
    echo "--- run dir (${RUN_DIR}) ---"
    cleanup
    exit 1
}

on_error() {
    local rc="$1"
    local line="$2"
    if [[ "${FAILING}" -eq 1 ]]; then
        exit "${rc}"
    fi
    fail "unexpected error (line=${line}, rc=${rc})"
}

trap 'on_error "$?" "$LINENO"' ERR

on_pipe() {
    if [[ "${FAILING}" -eq 1 ]]; then
        exit 141
    fi
    fail "SIGPIPE (broken pipe) while writing to client fifo (client likely exited)"
}

trap 'on_pipe' PIPE

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
[[ "${PROGRESS_INTERVAL_SEC}" =~ ^[0-9]+$ ]] || fail "test.longrun.progress_interval_sec must be numeric"
[[ "${CONNECT_WAIT_TRY}" =~ ^[0-9]+$ ]] || fail "test.longrun.connect_wait_try must be numeric"
[[ "${CONNECT_WAIT_TRY}" -gt 0 ]] || fail "test.longrun.connect_wait_try must be > 0"
[[ "${CLOSE_WAIT_SEC}" =~ ^[0-9]+$ ]] || fail "test.longrun.close_wait_sec must be numeric"
[[ "${CLOSE_WAIT_SEC}" -gt 0 ]] || fail "test.longrun.close_wait_sec must be > 0"
[[ "${MIN_UNIQUE_SENDERS_PER_CLIENT}" =~ ^[0-9]+$ ]] || fail "test.longrun.min_unique_senders_per_client must be numeric"
[[ "${MIN_UNIQUE_SENDERS_PER_CLIENT}" -gt 0 ]] || fail "test.longrun.min_unique_senders_per_client must be > 0"
if [[ "${LONGRUN_CLIENT_COUNT}" -lt "${MIN_UNIQUE_SENDERS_PER_CLIENT}" ]]; then
    MIN_UNIQUE_SENDERS_PER_CLIENT="${LONGRUN_CLIENT_COUNT}"
fi

precleanup_stale_longrun_processes

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
    client_log="$(make_timestamped_path "${RUN_DIR}" "tls-client-concurrent-${client_idx}" "log")"
    CLIENT_LOGS+=("${client_log}")
    CLIENT_MSG_SEQ+=("1")
    CLIENT_IDS+=("${client_id}")
    CLIENT_PWS+=("${client_pw}")

    client_argv0="${CLIENT_ARGV0_PREFIX}-${client_idx}"
    set +e
    exec -a "${client_argv0}" "${CLIENT_BIN}" "${CLIENT_IP}" "${CLIENT_PORT}" < "${fifo_path}" > "${client_log}" 2>&1 &
    client_spawn_rc=$?
    set -e
    if [[ "${client_spawn_rc}" -ne 0 ]]; then
        fail "failed to spawn client ${client_idx} (fork/resource limit)"
    fi
    client_pid=$!
    CLIENT_PIDS+=("${client_pid}")

    set +e
    exec {fifo_fd}> "${fifo_path}"
    fifo_open_rc=$?
    set -e
    if [[ "${fifo_open_rc}" -ne 0 ]]; then
        fail "failed to open fifo writer for client ${client_idx} (fork/resource limit)"
    fi
    CLIENT_FIFO_FDS+=("${fifo_fd}")

    if ! printf '/register %s %s\n' "${client_id}" "${client_pw}" >&${fifo_fd}; then
        fail "failed to write register command to client ${client_idx} fifo"
    fi

    {
        echo "[CLIENT ${client_idx}] id=${client_id} pid=${client_pid} log=${client_log}"
    } >> "${MAIN_CLIENT_LOG}"

    if [[ "${CONNECT_STAGGER_SEC}" != "0" ]]; then
        sleep "${CONNECT_STAGGER_SEC}"
    fi
done

if [[ "${REGISTER_LOGIN_DELAY_SEC}" != "0" ]]; then
    sleep "${REGISTER_LOGIN_DELAY_SEC}"
fi

for ((i=0; i<${#CLIENT_FIFO_FDS[@]}; ++i)); do
    fd="${CLIENT_FIFO_FDS[$i]}"
    client_idx=$((i + 1))
    client_id="${CLIENT_IDS[$i]}"
    client_pw="${CLIENT_PWS[$i]}"
    if ! printf '/login %s %s\n' "${client_id}" "${client_pw}" >&${fd}; then
        fail "failed to write login command to client ${client_idx} fifo"
    fi
done

if [[ "${LOGIN_SETTLE_SEC}" != "0" ]]; then
    sleep "${LOGIN_SETTLE_SEC}"
fi

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
hold_start="${SECONDS}"
hold_end=$((SECONDS + LONGRUN_DURATION_SEC))
next_progress_mark="${PROGRESS_INTERVAL_SEC}"
last_progress_reported=0
while [[ "${SECONDS}" -lt "${hold_end}" ]]; do
    if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
        fail "server crashed during longrun hold"
    fi

    if [[ "${PROGRESS_INTERVAL_SEC}" -gt 0 ]]; then
        elapsed_sec=$((SECONDS - hold_start))
        while [[ "${next_progress_mark}" -le "${LONGRUN_DURATION_SEC}" && "${elapsed_sec}" -ge "${next_progress_mark}" ]]; do
            echo "[INFO] hold progress: ${next_progress_mark}/${LONGRUN_DURATION_SEC}s"
            last_progress_reported="${next_progress_mark}"
            next_progress_mark=$((next_progress_mark + PROGRESS_INTERVAL_SEC))
        done
    fi

    for ((i=0; i<${#CLIENT_FIFO_FDS[@]}; ++i)); do
        fd="${CLIENT_FIFO_FDS[$i]}"
        client_idx=$((i + 1))
        msg_seq="${CLIENT_MSG_SEQ[$i]}"
        if ! printf '%s-%s-%s\n' "${MESSAGE_PREFIX}" "${client_idx}" "${msg_seq}" >&${fd}; then
            fail "failed to write longrun message to client ${client_idx} fifo"
        fi
        CLIENT_MSG_SEQ[$i]=$((msg_seq + 1))
    done

    if [[ "${MESSAGE_INTERVAL_SEC}" != "0" ]]; then
        sleep "${MESSAGE_INTERVAL_SEC}"
    else
        sleep "${CHECK_INTERVAL_SEC}"
    fi
done

if [[ "${PROGRESS_INTERVAL_SEC}" -gt 0 && "${last_progress_reported}" -lt "${LONGRUN_DURATION_SEC}" ]]; then
    echo "[INFO] hold progress: ${LONGRUN_DURATION_SEC}/${LONGRUN_DURATION_SEC}s"
fi

echo "[INFO] hold finished; closing clients"
for fd in "${CLIENT_FIFO_FDS[@]}"; do
    [[ -z "${fd}" ]] && continue
    eval "exec ${fd}>&-" 2>/dev/null || true
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
