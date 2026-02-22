#include "net/tls_session.hpp"
#include "net/tls_error.hpp"
#include <cerrno>
#include <cctype>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>
#include <string>

int tls_session_last_reason(){
    unsigned long err = ::ERR_peek_last_error();
    if(err == 0) return 0;
    return static_cast<int>(::ERR_GET_REASON(err));
}

std::string tls_session_to_lower(std::string s){
    for(char& ch : s){
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

tls::tls_error classify_ssl_reason(int reason){
    if(reason == 0) return tls::tls_error::ssl_library_error;

#ifdef SSL_R_CERTIFICATE_VERIFY_FAILED
    if(reason == SSL_R_CERTIFICATE_VERIFY_FAILED) return tls::tls_error::verify_failed;
#endif

    const char* reason_text = ::ERR_reason_error_string(static_cast<unsigned long>(reason));
    if(reason_text == nullptr) return tls::tls_error::ssl_library_error;

    std::string lowered = tls_session_to_lower(reason_text);
    if(lowered.find("hostname") != std::string::npos){
        return tls::tls_error::verify_hostname_mismatch;
    }
    if(lowered.find("certificate") != std::string::npos || lowered.find("cert") != std::string::npos){
        return tls::tls_error::verify_failed;
    }
    if(lowered.find("verify") != std::string::npos){
        return tls::tls_error::verify_failed;
    }
    if(lowered.find("handshake") != std::string::npos){
        return tls::tls_error::handshake_failed;
    }
    if(lowered.find("alert") != std::string::npos){
        return tls::tls_error::alert_received;
    }
    if(lowered.find("protocol") != std::string::npos || lowered.find("version") != std::string::npos){
        return tls::tls_error::protocol_error;
    }
    return tls::tls_error::ssl_library_error;
}

error_code make_tls_session_error(tls::tls_error kind){
    return error_code::from_tls(tls::make_code(kind, tls_session_last_reason()));
}

error_code make_tls_verify_error(tls::tls_error kind, long verify_rc){
    return error_code::from_tls(tls::make_code(kind, static_cast<int>(verify_rc)));
}

void tls_session::ssl_deleter::operator()(SSL* p) const noexcept{
    if(p) ::SSL_free(p);
}

tls_session::tls_session(std::unique_ptr<SSL, ssl_deleter> ssl) noexcept :
    ssl(std::move(ssl)){}

std::expected <tls_io_result, error_code> tls_session::from_ssl_error(int ssl_error, std::size_t byte){
    if(ssl_error == SSL_ERROR_WANT_READ){
        return tls_io_result{.byte = byte, .closed = false, .want_read = true, .want_write = false};
    }

    if(ssl_error == SSL_ERROR_WANT_WRITE){
        return tls_io_result{.byte = byte, .closed = false, .want_read = false, .want_write = true};
    }

    if(ssl_error == SSL_ERROR_ZERO_RETURN){
        return tls_io_result{.byte = byte, .closed = true, .want_read = false, .want_write = false};
    }

    if(ssl_error == SSL_ERROR_SYSCALL){
        int ec = errno;
        if(ec == 0 || ec == ECONNRESET || ec == EPIPE || ec == ENOTCONN){
            return tls_io_result{
                .byte = byte,
                .closed = true,
                .want_read = false,
                .want_write = false
            };
        }
        return std::unexpected(error_code::from_errno(ec));
    }

    if(ssl_error == SSL_ERROR_SSL){
        int reason = tls_session_last_reason();
        tls::tls_error kind = classify_ssl_reason(reason);
        return std::unexpected(error_code::from_tls(tls::make_code(kind, reason)));
    }

    return std::unexpected(make_tls_session_error(tls::tls_error::protocol_error));
}

void tls_session::apply_state(const tls_io_result& io) noexcept{
    want_read = io.want_read;
    want_write = io.want_write;
    if(io.closed) peer_closed = true;
}

std::expected <tls_session, error_code> tls_session::create_server(tls_context& ctx, int fd){
    if(fd < 0 || !ctx.is_server()) return std::unexpected(error_code::from_errno(EINVAL));

    ::ERR_clear_error();
    SSL* raw = ::SSL_new(ctx.get());
    if(raw == nullptr) return std::unexpected(make_tls_session_error(tls::tls_error::ctx_create_failed));

    std::unique_ptr<SSL, ssl_deleter> ssl(raw);
    ::ERR_clear_error();
    if(::SSL_set_fd(ssl.get(), fd) != 1){
        return std::unexpected(make_tls_session_error(tls::tls_error::set_fd_failed));
    }

    ::SSL_set_accept_state(ssl.get());
    return tls_session(std::move(ssl));
}

std::expected <tls_session, error_code> tls_session::create_client(
    tls_context& ctx, int fd, std::string_view server_name
){
    if(fd < 0 || ctx.is_server() || server_name.empty()){
        return std::unexpected(error_code::from_errno(EINVAL));
    }

    ::ERR_clear_error();
    SSL* raw = ::SSL_new(ctx.get());
    if(raw == nullptr) return std::unexpected(make_tls_session_error(tls::tls_error::ctx_create_failed));

    std::unique_ptr<SSL, ssl_deleter> ssl(raw);
    ::ERR_clear_error();
    if(::SSL_set_fd(ssl.get(), fd) != 1){
        return std::unexpected(make_tls_session_error(tls::tls_error::set_fd_failed));
    }

    std::string host(server_name);
    ::ERR_clear_error();
    if(::SSL_set_tlsext_host_name(ssl.get(), host.c_str()) != 1){
        return std::unexpected(make_tls_session_error(tls::tls_error::set_sni_failed));
    }

    ::ERR_clear_error();
    if(::SSL_set1_host(ssl.get(), host.c_str()) != 1){
        return std::unexpected(make_tls_session_error(tls::tls_error::set_host_failed));
    }

    ::SSL_set_connect_state(ssl.get());
    return tls_session(std::move(ssl));
}

std::expected <tls_io_result, error_code> tls_session::handshake(){
    if(ssl == nullptr) return std::unexpected(error_code::from_errno(EINVAL));
    if(handshake_done){
        tls_io_result ret{};
        apply_state(ret);
        return ret;
    }

    ::ERR_clear_error();
    int rc = ::SSL_do_handshake(ssl.get());
    if(rc == 1){
        handshake_done = true;
        tls_io_result ret{};
        apply_state(ret);
        return ret;
    }

    auto io_exp = from_ssl_error(::SSL_get_error(ssl.get(), rc), 0);
    if(io_exp) apply_state(*io_exp);
    return io_exp;
}

std::expected <tls_io_result, error_code> tls_session::read(char* dst, std::size_t cap){
    if(ssl == nullptr) return std::unexpected(error_code::from_errno(EINVAL));
    if(cap == 0){
        tls_io_result ret{};
        apply_state(ret);
        return ret;
    }

    std::size_t byte = 0;
    ::ERR_clear_error();
    int rc = ::SSL_read_ex(ssl.get(), dst, cap, &byte);
    if(rc == 1){
        tls_io_result ret{.byte = byte, .closed = false, .want_read = false, .want_write = false};
        apply_state(ret);
        return ret;
    }

    auto io_exp = from_ssl_error(::SSL_get_error(ssl.get(), rc), byte);
    if(io_exp) apply_state(*io_exp);
    return io_exp;
}

std::expected <tls_io_result, error_code> tls_session::write(const char* src, std::size_t len){
    if(ssl == nullptr) return std::unexpected(error_code::from_errno(EINVAL));
    if(len == 0){
        tls_io_result ret{};
        apply_state(ret);
        return ret;
    }

    std::size_t byte = 0;
    ::ERR_clear_error();
    int rc = ::SSL_write_ex(ssl.get(), src, len, &byte);
    if(rc == 1){
        tls_io_result ret{.byte = byte, .closed = false, .want_read = false, .want_write = false};
        apply_state(ret);
        return ret;
    }

    auto io_exp = from_ssl_error(::SSL_get_error(ssl.get(), rc), byte);
    if(io_exp) apply_state(*io_exp);
    return io_exp;
}

std::expected <void, error_code> tls_session::shutdown(){
    if(ssl == nullptr) return {};

    ::ERR_clear_error();
    int rc = ::SSL_shutdown(ssl.get());
    if(rc == 1) return {};
    if(rc == 0) return {};

    int ssl_error = ::SSL_get_error(ssl.get(), rc);
    if(ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE){
        return {};
    }

    if(ssl_error == SSL_ERROR_ZERO_RETURN) return {};

    if(ssl_error == SSL_ERROR_SYSCALL){
        int ec = errno;
        if(ec == 0) return {};
        return std::unexpected(error_code::from_errno(ec));
    }

    if(ssl_error == SSL_ERROR_SSL){
        int reason = tls_session_last_reason();
        return std::unexpected(
            error_code::from_tls(tls::make_code(tls::tls_error::shutdown_failed, reason))
        );
    }
    return std::unexpected(make_tls_session_error(tls::tls_error::shutdown_failed));
}

std::expected <void, error_code> tls_session::verify_peer() const{
    if(ssl == nullptr) return std::unexpected(error_code::from_errno(EINVAL));
    X509* cert = ::SSL_get1_peer_certificate(ssl.get());
    if(cert == nullptr){
        return std::unexpected(make_tls_verify_error(tls::tls_error::verify_no_peer_cert, 0));
    }
    ::X509_free(cert);

    long verify_rc = ::SSL_get_verify_result(ssl.get());
    if(verify_rc != X509_V_OK){
#ifdef X509_V_ERR_HOSTNAME_MISMATCH
        if(verify_rc == X509_V_ERR_HOSTNAME_MISMATCH){
            return std::unexpected(
                make_tls_verify_error(tls::tls_error::verify_hostname_mismatch, verify_rc)
            );
        }
#endif
        if(verify_rc == X509_V_ERR_CERT_HAS_EXPIRED){
            return std::unexpected(
                make_tls_verify_error(tls::tls_error::verify_cert_expired, verify_rc)
            );
        }
        if(verify_rc == X509_V_ERR_CERT_NOT_YET_VALID){
            return std::unexpected(
                make_tls_verify_error(tls::tls_error::verify_cert_not_yet_valid, verify_rc)
            );
        }
        return std::unexpected(make_tls_verify_error(tls::tls_error::verify_failed, verify_rc));
    }
    return {};
}

bool tls_session::is_handshake_done() const noexcept{ return handshake_done; }
bool tls_session::needs_read() const noexcept{ return want_read; }
bool tls_session::needs_write() const noexcept{ return want_write; }
bool tls_session::is_closed() const noexcept{ return peer_closed; }
SSL* tls_session::get() const noexcept{ return ssl.get(); }
