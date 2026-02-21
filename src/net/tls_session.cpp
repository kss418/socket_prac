#include "net/tls_session.hpp"
#include <cerrno>
#include <openssl/ssl.h>
#include <string>

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
        if(ec == 0) ec = EPROTO;
        return std::unexpected(error_code::from_errno(ec));
    }

    return std::unexpected(error_code::from_errno(EPROTO));
}

void tls_session::apply_state(const tls_io_result& io) noexcept{
    want_read = io.want_read;
    want_write = io.want_write;
    if(io.closed) peer_closed = true;
}

std::expected <tls_session, error_code> tls_session::create_server(tls_context& ctx, int fd){
    if(fd < 0 || !ctx.is_server()) return std::unexpected(error_code::from_errno(EINVAL));

    SSL* raw = ::SSL_new(ctx.get());
    if(raw == nullptr) return std::unexpected(error_code::from_errno(EPROTO));

    std::unique_ptr<SSL, ssl_deleter> ssl(raw);
    if(::SSL_set_fd(ssl.get(), fd) != 1) return std::unexpected(error_code::from_errno(EPROTO));

    ::SSL_set_accept_state(ssl.get());
    return tls_session(std::move(ssl));
}

std::expected <tls_session, error_code> tls_session::create_client(
    tls_context& ctx, int fd, std::string_view server_name
){
    if(fd < 0 || ctx.is_server() || server_name.empty()){
        return std::unexpected(error_code::from_errno(EINVAL));
    }

    SSL* raw = ::SSL_new(ctx.get());
    if(raw == nullptr) return std::unexpected(error_code::from_errno(EPROTO));

    std::unique_ptr<SSL, ssl_deleter> ssl(raw);
    if(::SSL_set_fd(ssl.get(), fd) != 1) return std::unexpected(error_code::from_errno(EPROTO));

    std::string host(server_name);
    if(::SSL_set_tlsext_host_name(ssl.get(), host.c_str()) != 1){
        return std::unexpected(error_code::from_errno(EPROTO));
    }

    if(::SSL_set1_host(ssl.get(), host.c_str()) != 1){
        return std::unexpected(error_code::from_errno(EPROTO));
    }

    ::SSL_set_connect_state(ssl.get());
    return tls_session(std::move(ssl));
}

std::expected <tls_io_result, error_code> tls_session::handshake(){
    if(handshake_done){
        tls_io_result ret{};
        apply_state(ret);
        return ret;
    }

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
    if(cap == 0){
        tls_io_result ret{};
        apply_state(ret);
        return ret;
    }

    std::size_t byte = 0;
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
    if(len == 0){
        tls_io_result ret{};
        apply_state(ret);
        return ret;
    }

    std::size_t byte = 0;
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

std::expected <void, error_code> tls_session::verify_peer() const{
    X509* cert = ::SSL_get1_peer_certificate(ssl.get());
    if(cert == nullptr) return std::unexpected(error_code::from_errno(EPERM));
    ::X509_free(cert);

    long verify_rc = ::SSL_get_verify_result(ssl.get());
    if(verify_rc != X509_V_OK) return std::unexpected(error_code::from_errno(EPERM));
    return {};
}

bool tls_session::is_handshake_done() const noexcept{ return handshake_done; }
bool tls_session::needs_read() const noexcept{ return want_read; }
bool tls_session::needs_write() const noexcept{ return want_write; }
bool tls_session::is_closed() const noexcept{ return peer_closed; }
SSL* tls_session::get() const noexcept{ return ssl.get(); }
