#!/usr/bin/env bash

pg_psql() {
    local host="$1"
    local port="$2"
    local user="$3"
    local db="$4"
    local password="$5"
    local sslmode="$6"
    local connect_timeout="$7"
    shift 7

    local -a args=(
        --no-psqlrc
        -v ON_ERROR_STOP=1
    )
    [[ -n "${host}" ]] && args+=(--host="${host}")
    [[ -n "${port}" ]] && args+=(--port="${port}")
    [[ -n "${user}" ]] && args+=(--username="${user}")
    [[ -n "${db}" ]] && args+=(--dbname="${db}")

    PGPASSWORD="${password}" \
    PGSSLMODE="${sslmode}" \
    PGCONNECT_TIMEOUT="${connect_timeout}" \
    psql "${args[@]}" "$@"
}

