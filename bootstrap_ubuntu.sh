#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ -r /etc/os-release ]]; then
    # shellcheck disable=SC1091
    source /etc/os-release
else
    echo "[FAIL] /etc/os-release not found"
    exit 1
fi

if [[ "${ID:-}" != "ubuntu" && "${ID_LIKE:-}" != *"ubuntu"* ]]; then
    echo "[FAIL] this script supports Ubuntu only"
    echo "[INFO] detected id=${ID:-unknown} id_like=${ID_LIKE:-unknown}"
    exit 1
fi

if ! command -v apt-get >/dev/null 2>&1; then
    echo "[FAIL] apt-get command not found"
    exit 1
fi

UPDATE_APT="${UPDATE_APT:-1}"
INSTALL_POSTGRES="${INSTALL_POSTGRES:-1}"
INSTALL_DEV_HEADERS="${INSTALL_DEV_HEADERS:-1}"
PROVISION_DB="${PROVISION_DB:-1}"
MIGRATE_DB_SCHEMA="${MIGRATE_DB_SCHEMA:-1}"
GENERATE_TLS_CERTS="${GENERATE_TLS_CERTS:-auto}"
BUILD_SERVER="${BUILD_SERVER:-1}"
SERVER_CONFIG_PATH="${SERVER_CONFIG_PATH:-config/server.conf}"
ENV_FILE_PATH="${ENV_FILE_PATH:-.env}"

to_bool() {
    case "${1,,}" in
        1|true|yes|on) echo "1" ;;
        *) echo "0" ;;
    esac
}

warn() {
    echo "[WARN] $1"
}

SUDO=""
if [[ "$(id -u)" -ne 0 ]]; then
    if command -v sudo >/dev/null 2>&1; then
        SUDO="sudo"
    else
        echo "[FAIL] run as root or install sudo"
        exit 1
    fi
fi

resolve_path_from_root() {
    local p="$1"
    if [[ -z "${p}" ]]; then
        echo ""
    elif [[ "${p}" = /* ]]; then
        echo "${p}"
    else
        echo "${PROJECT_ROOT}/${p}"
    fi
}

cfg_get_from_file_simple() {
    local cfg="$1"
    local key="$2"
    if [[ ! -f "${cfg}" ]]; then
        echo ""
        return 0
    fi
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
            exit
        }
    }
    ' "${cfg}"
}

run_as_invoker_in_project() {
    local cmd="$1"
    if [[ "$(id -u)" -eq 0 && -n "${SUDO_USER:-}" && "${SUDO_USER}" != "root" ]]; then
        if command -v runuser >/dev/null 2>&1; then
            runuser -u "${SUDO_USER}" -- bash -lc "cd '${PROJECT_ROOT}' && ${cmd}"
            return $?
        fi
        if command -v su >/dev/null 2>&1; then
            su -s /bin/bash "${SUDO_USER}" -c "cd '${PROJECT_ROOT}' && ${cmd}"
            return $?
        fi
        echo "[FAIL] runuser/su not found; cannot run as ${SUDO_USER}"
        exit 1
    fi
    (
        cd "${PROJECT_ROOT}"
        bash -lc "${cmd}"
    )
}

should_generate_tls_certs() {
    local mode="${GENERATE_TLS_CERTS,,}"
    case "${mode}" in
        1|true|yes|on)
            return 0
            ;;
        0|false|no|off)
            return 1
            ;;
        auto|"")
            local cfg_abs cert_rel key_rel cert_path key_path
            cfg_abs="$(resolve_path_from_root "${SERVER_CONFIG_PATH}")"
            if [[ ! -f "${cfg_abs}" ]]; then
                warn "server config not found (${cfg_abs}); generating tls certs"
                return 0
            fi
            cert_rel="$(cfg_get_from_file_simple "${cfg_abs}" "tls.cert")"
            key_rel="$(cfg_get_from_file_simple "${cfg_abs}" "tls.key")"
            if [[ -z "${cert_rel}" || -z "${key_rel}" ]]; then
                warn "tls.cert/tls.key missing in ${cfg_abs}; generating tls certs"
                return 0
            fi
            cert_path="$(resolve_path_from_root "${cert_rel}")"
            key_path="$(resolve_path_from_root "${key_rel}")"
            if [[ -f "${cert_path}" && -f "${key_path}" ]]; then
                return 1
            fi
            return 0
            ;;
        *)
            echo "[FAIL] invalid GENERATE_TLS_CERTS=${GENERATE_TLS_CERTS} (use auto|1|0)"
            exit 1
            ;;
    esac
}

install_packages() {
    local packages=("$@")
    if [[ "${#packages[@]}" -eq 0 ]]; then
        return 0
    fi
    if [[ -n "${SUDO}" ]]; then
        DEBIAN_FRONTEND=noninteractive sudo apt-get install -y --no-install-recommends "${packages[@]}"
    else
        DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${packages[@]}"
    fi
}

BASE_PACKAGES=(
    ca-certificates
    curl
    git
    pkg-config
    build-essential
    cmake
    ninja-build
    zip
    unzip
    tar
    openssl
)

DEV_HEADER_PACKAGES=(
    libssl-dev
    libpq-dev
)

POSTGRES_PACKAGES=(
    postgresql
    postgresql-client
    postgresql-contrib
)

echo "[INFO] Ubuntu bootstrap start"
echo "[INFO] update_apt=$(to_bool "${UPDATE_APT}") install_postgres=$(to_bool "${INSTALL_POSTGRES}") install_dev_headers=$(to_bool "${INSTALL_DEV_HEADERS}")"
echo "[INFO] provision_db=$(to_bool "${PROVISION_DB}") migrate_db_schema=$(to_bool "${MIGRATE_DB_SCHEMA}")"
echo "[INFO] generate_tls_certs=${GENERATE_TLS_CERTS} build_server=$(to_bool "${BUILD_SERVER}")"

if [[ "$(to_bool "${UPDATE_APT}")" == "1" ]]; then
    echo "[INFO] apt-get update"
    ${SUDO} apt-get update
fi

echo "[INFO] installing base packages"
install_packages "${BASE_PACKAGES[@]}"

if [[ "$(to_bool "${INSTALL_DEV_HEADERS}")" == "1" ]]; then
    echo "[INFO] installing development header packages"
    install_packages "${DEV_HEADER_PACKAGES[@]}"
fi

if [[ "$(to_bool "${INSTALL_POSTGRES}")" == "1" ]]; then
    echo "[INFO] installing PostgreSQL packages"
    install_packages "${POSTGRES_PACKAGES[@]}"

    # Start service only when service control is available.
    if command -v systemctl >/dev/null 2>&1; then
        if ${SUDO} systemctl list-unit-files | grep -q '^postgresql\.service'; then
            ${SUDO} systemctl enable --now postgresql || true
        fi
    elif command -v service >/dev/null 2>&1; then
        ${SUDO} service postgresql start || true
    fi
fi

if should_generate_tls_certs; then
    echo "[INFO] generating TLS certificates"
    run_as_invoker_in_project "./scripts/generate_tls_certs.sh"
else
    echo "[INFO] skip TLS certificate generation (already present)"
fi

if [[ "$(to_bool "${PROVISION_DB}")" == "1" ]]; then
    echo "[INFO] provisioning DB role/database"
    "${PROJECT_ROOT}/scripts/provision_db_role.sh" "${SERVER_CONFIG_PATH}" "${ENV_FILE_PATH}"
fi

if [[ "$(to_bool "${MIGRATE_DB_SCHEMA}")" == "1" ]]; then
    echo "[INFO] applying DB schema migration"
    "${PROJECT_ROOT}/scripts/migrate_db_schema.sh" "${SERVER_CONFIG_PATH}" "${ENV_FILE_PATH}"
fi

if [[ "$(to_bool "${BUILD_SERVER}")" == "1" ]]; then
    echo "[INFO] building server binary"
    run_as_invoker_in_project "./scripts/build_server.sh"
fi

echo "[INFO] tool versions"
cmake --version | head -n 1 || true
g++ --version | head -n 1 || true
psql --version || true
openssl version || true

echo "[PASS] Ubuntu bootstrap finished"
echo "[INFO] next steps:"
echo "  1) ./build/server"
echo "  2) ./scripts/test/test_integration.sh"
