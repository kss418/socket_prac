#include "net/tls_error.hpp"

#include <openssl/err.h>
#include <openssl/x509_vfy.h>

namespace tls{
    static std::string kind_to_string(tls_error kind){
        switch(kind){
            case tls_error::unknown:
                return "tls.unknown";
            case tls_error::openssl_init_failed:
                return "tls.openssl_init_failed";
            case tls_error::min_protocol_set_failed:
                return "tls.min_protocol_set_failed";
            case tls_error::ctx_create_failed:
                return "tls.ctx_create_failed";
            case tls_error::cert_chain_load_failed:
                return "tls.cert_chain_load_failed";
            case tls_error::private_key_load_failed:
                return "tls.private_key_load_failed";
            case tls_error::private_key_check_failed:
                return "tls.private_key_check_failed";
            case tls_error::default_verify_paths_failed:
                return "tls.default_verify_paths_failed";
            case tls_error::ca_load_failed:
                return "tls.ca_load_failed";
            case tls_error::set_fd_failed:
                return "tls.set_fd_failed";
            case tls_error::set_sni_failed:
                return "tls.set_sni_failed";
            case tls_error::set_host_failed:
                return "tls.set_host_failed";
            case tls_error::handshake_failed:
                return "tls.handshake_failed";
            case tls_error::ssl_library_error:
                return "tls.ssl_library_error";
            case tls_error::alert_received:
                return "tls.alert_received";
            case tls_error::protocol_error:
                return "tls.protocol_error";
            case tls_error::shutdown_failed:
                return "tls.shutdown_failed";
            case tls_error::verify_no_peer_cert:
                return "tls.verify_no_peer_cert";
            case tls_error::verify_failed:
                return "tls.verify_failed";
            case tls_error::verify_hostname_mismatch:
                return "tls.verify_hostname_mismatch";
            case tls_error::verify_cert_expired:
                return "tls.verify_cert_expired";
            case tls_error::verify_cert_not_yet_valid:
                return "tls.verify_cert_not_yet_valid";
        }
        return "tls.unknown";
    }

    static std::string reason_to_string(tls_error kind, int reason){
        if(reason == 0) return {};

        if(
            kind == tls_error::verify_failed
            || kind == tls_error::verify_hostname_mismatch
            || kind == tls_error::verify_cert_expired
            || kind == tls_error::verify_cert_not_yet_valid
            || kind == tls_error::verify_no_peer_cert
        ){
            const char* verify_reason = ::X509_verify_cert_error_string(reason);
            if(verify_reason != nullptr && verify_reason[0] != '\0'){
                return std::string(verify_reason);
            }
        }

        const char* openssl_reason = ::ERR_reason_error_string(static_cast<unsigned long>(reason));
        if(openssl_reason != nullptr && openssl_reason[0] != '\0'){
            return std::string(openssl_reason);
        }

        return {};
    }

    std::string tls_strerror(int code){
        tls_error kind = kind_of(code);
        int reason = reason_of(code);
        std::string out = kind_to_string(kind);

        std::string reason_str = reason_to_string(kind, reason);
        if(!reason_str.empty()){
            out += " (reason: ";
            out += reason_str;
            out += ")";
        }
        return out;
    }
}
