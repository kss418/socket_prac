#include "net/tls_context.hpp"
#include "net/tls_error.hpp"
#include <cerrno>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <string>

int tls_context_last_reason(){
    unsigned long err = ::ERR_peek_last_error();
    if(err == 0) return 0;
    return static_cast<int>(::ERR_GET_REASON(err));
}

error_code make_tls_context_error(tls::tls_error kind){
    return error_code::from_tls(tls::make_code(kind, tls_context_last_reason()));
}

std::expected <void, error_code> tls_context::init_tls(){
    ::ERR_clear_error();
    if(::OPENSSL_init_ssl(0, nullptr) == 1) return {};
    return std::unexpected(make_tls_context_error(tls::tls_error::openssl_init_failed));
}

std::expected <void, error_code> tls_context::set_common_options(SSL_CTX* ctx){
    ::ERR_clear_error();
    if(::SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION) != 1){
        return std::unexpected(make_tls_context_error(tls::tls_error::min_protocol_set_failed));
    }

    long options = SSL_OP_NO_COMPRESSION;
#ifdef SSL_OP_IGNORE_UNEXPECTED_EOF
    options |= SSL_OP_IGNORE_UNEXPECTED_EOF;
#endif
    ::SSL_CTX_set_options(ctx, options);
    ::SSL_CTX_set_mode(ctx, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    return {};
}

void tls_context::ctx_deleter::operator()(SSL_CTX* p) const noexcept{
    if(p) ::SSL_CTX_free(p);
}

tls_context::tls_context(std::unique_ptr<SSL_CTX, ctx_deleter> ctx, bool server) noexcept :
    ctx(std::move(ctx)), server(server){}

std::expected <tls_context, error_code> tls_context::create_server(
    std::string_view cert_chain_path,
    std::string_view private_key_path
){
    auto init_exp = tls_context::init_tls();
    if(!init_exp) return std::unexpected(init_exp.error());

    if(cert_chain_path.empty() || private_key_path.empty()){
        return std::unexpected(error_code::from_errno(EINVAL));
    }

    ::ERR_clear_error();
    SSL_CTX* raw = ::SSL_CTX_new(::TLS_server_method());
    if(raw == nullptr) return std::unexpected(make_tls_context_error(tls::tls_error::ctx_create_failed));

    std::unique_ptr<SSL_CTX, ctx_deleter> ctx(raw);
    auto common_exp = tls_context::set_common_options(ctx.get());
    if(!common_exp) return std::unexpected(common_exp.error());

    std::string cert(cert_chain_path);
    std::string key(private_key_path);
    ::ERR_clear_error();
    if(::SSL_CTX_use_certificate_chain_file(ctx.get(), cert.c_str()) != 1){
        return std::unexpected(make_tls_context_error(tls::tls_error::cert_chain_load_failed));
    }

    ::ERR_clear_error();
    if(::SSL_CTX_use_PrivateKey_file(ctx.get(), key.c_str(), SSL_FILETYPE_PEM) != 1){
        return std::unexpected(make_tls_context_error(tls::tls_error::private_key_load_failed));
    }

    ::ERR_clear_error();
    if(::SSL_CTX_check_private_key(ctx.get()) != 1){
        return std::unexpected(make_tls_context_error(tls::tls_error::private_key_check_failed));
    }

    return tls_context(std::move(ctx), true);
}

std::expected <tls_context, error_code> tls_context::create_client(std::string_view ca_file_path){
    auto init_exp = tls_context::init_tls();
    if(!init_exp) return std::unexpected(init_exp.error());

    ::ERR_clear_error();
    SSL_CTX* raw = ::SSL_CTX_new(::TLS_client_method());
    if(raw == nullptr) return std::unexpected(make_tls_context_error(tls::tls_error::ctx_create_failed));

    std::unique_ptr<SSL_CTX, ctx_deleter> ctx(raw);
    auto common_exp = tls_context::set_common_options(ctx.get());
    if(!common_exp) return std::unexpected(common_exp.error());

    ::SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_PEER, nullptr);
    if(ca_file_path.empty()){
        ::ERR_clear_error();
        if(::SSL_CTX_set_default_verify_paths(ctx.get()) != 1){
            return std::unexpected(make_tls_context_error(tls::tls_error::default_verify_paths_failed));
        }
    }
    else{
        std::string ca_file(ca_file_path);
        ::ERR_clear_error();
        if(::SSL_CTX_load_verify_locations(ctx.get(), ca_file.c_str(), nullptr) != 1){
            return std::unexpected(make_tls_context_error(tls::tls_error::ca_load_failed));
        }
    }

    return tls_context(std::move(ctx), false);
}

SSL_CTX* tls_context::get() const noexcept{ return ctx.get(); }
bool tls_context::is_server() const noexcept{ return server; }
