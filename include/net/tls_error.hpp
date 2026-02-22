#pragma once

#include <string>

namespace tls{
    enum class tls_error : int{
        unknown = 1,
        openssl_init_failed,
        min_protocol_set_failed,
        ctx_create_failed,
        cert_chain_load_failed,
        private_key_load_failed,
        private_key_check_failed,
        default_verify_paths_failed,
        ca_load_failed,
        set_fd_failed,
        set_sni_failed,
        set_host_failed,
        handshake_failed,
        ssl_library_error,
        alert_received,
        protocol_error,
        shutdown_failed,
        verify_no_peer_cert,
        verify_failed,
        verify_hostname_mismatch,
        verify_cert_expired,
        verify_cert_not_yet_valid
    };

    constexpr int tls_kind_shift = 16;
    constexpr int tls_reason_mask = 0xFFFF;

    inline int make_code(tls_error kind, int reason = 0){
        return (static_cast<int>(kind) << tls_kind_shift) | (reason & tls_reason_mask);
    }

    inline tls_error kind_of(int code){
        if(code <= 0) return tls_error::unknown;
        return static_cast<tls_error>((code >> tls_kind_shift) & tls_reason_mask);
    }

    inline int reason_of(int code){
        return code & tls_reason_mask;
    }

    std::string tls_strerror(int code);
}
