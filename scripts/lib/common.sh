#!/usr/bin/env bash

cfg_get_from_file() {
    local key="$1"
    local fallback="${2:-}"
    local cfg_file="${3:-}"
    local value=""

    if [[ -n "${cfg_file}" ]] && [[ -f "${cfg_file}" ]]; then
        value="$(
            awk -F= -v want="${key}" '
            /^[[:space:]]*#/ || /^[[:space:]]*$/ { next }
            {
                k=$1
                sub(/^[[:space:]]+/, "", k)
                sub(/[[:space:]]+$/, "", k)
                if (k == want) {
                    v=substr($0, index($0, "=") + 1)
                    sub(/^[[:space:]]+/, "", v)
                    sub(/[[:space:]]+$/, "", v)
                    print v
                    found=1
                    exit
                }
            }
            END {
                if (!found) exit 1
            }
            ' "${cfg_file}" 2>/dev/null
        )" || true
    fi

    if [[ -z "${value}" ]]; then
        echo "${fallback}"
    else
        echo "${value}"
    fi
}

cfg_get() {
    cfg_get_from_file "$1" "${2:-}" "${CONFIG_FILE:-}"
}

resolve_path_from_root() {
    local p="$1"
    if [[ -z "${p}" ]]; then
        echo ""
    elif [[ "${p}" = /* ]]; then
        echo "${p}"
    else
        echo "${ROOT_DIR}/${p}"
    fi
}

timestamp_now() {
    date '+%Y%m%d_%H%M%S'
}

make_timestamped_path() {
    local dir="$1"
    local prefix="$2"
    local ext="${3:-log}"
    local ts
    local path
    local seq=1

    ts="$(timestamp_now)"
    path="${dir}/${prefix}-${ts}.${ext}"

    while [[ -e "${path}" ]]; do
        path="${dir}/${prefix}-${ts}-${seq}.${ext}"
        seq=$((seq + 1))
    done

    echo "${path}"
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

to_bool() {
    local v="$1"
    case "${v,,}" in
        1|true|yes|on) echo "1" ;;
        *) echo "0" ;;
    esac
}

load_env_file() {
    local env_file="$1"
    if [[ -z "${env_file}" ]] || [[ ! -f "${env_file}" ]]; then
        return 0
    fi

    local line=""
    local key=""
    local value=""

    exec 9< "${env_file}"
    while IFS= read -r line <&9 || [[ -n "${line}" ]]; do
        # trim leading/trailing spaces
        line="${line#"${line%%[![:space:]]*}"}"
        line="${line%"${line##*[![:space:]]}"}"

        [[ -z "${line}" ]] && continue
        [[ "${line}" == \#* ]] && continue
        [[ "${line}" != *=* ]] && continue

        key="${line%%=*}"
        value="${line#*=}"
        key="${key#"${key%%[![:space:]]*}"}"
        key="${key%"${key##*[![:space:]]}"}"
        value="${value#"${value%%[![:space:]]*}"}"
        value="${value%"${value##*[![:space:]]}"}"
        value="$(trim_wrapping_quotes "${value}")"

        [[ -z "${key}" ]] && continue
        if [[ ! "${key}" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]; then
            continue
        fi
        export "${key}=${value}"
    done
    exec 9<&-

    return 0
}

